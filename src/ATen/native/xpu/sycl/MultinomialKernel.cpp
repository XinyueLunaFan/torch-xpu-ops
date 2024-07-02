#include <ATen/AccumulateType.h>
#include <ATen/core/Tensor.h>
#include <ATen/native/xpu/sycl/DistributionTemplates.h>
#include <ATen/native/xpu/sycl/MultinomialKernel.h>
#include <ATen/native/xpu/sycl/SYCLGroupAlgorithm.h>
#include <ATen/xpu/XPUGeneratorImpl.h>
#include <comm/Runtime.h>
#include <comm/SYCLContext.h>
#include <comm/SYCLHelpers.h>
namespace at::native::xpu {

template <typename scalar_t, typename item_t>
inline void renormRowsL1(
    item_t& item,
    scalar_t* dist,
    int64_t rows,
    int64_t cols,
    unsigned char* my_smem) {
  auto thread_idx = item.get_local_id(0);
  auto thread_range = item.get_local_range(0);
  auto group_idx = item.get_group(0);
  auto group_range = item.get_group_range(0);

  scalar_t* smem = reinterpret_cast<scalar_t*>(my_smem);
  scalar_t zero = static_cast<scalar_t>(0);
  scalar_t val;
  for (int64_t row = group_idx; row < rows; row += group_range) {
    scalar_t sum = static_cast<scalar_t>(0);
    for (int64_t col = thread_idx; col < cols; col += thread_range) {
      val = dist[row * cols + col];
      sum = sum + val;
    }

    sum = GroupReduceSumSGSizeEqualstoNumSG(item, sum, smem);
    if (thread_idx == 0) {
      smem[0] = sum;
    }
    item.barrier(sycl_local_fence);

    sum = smem[0];
    if (sum > zero) {
      for (int64_t col = thread_idx; col < cols; col += thread_range) {
        dist[row * cols + col] = dist[row * cols + col] / sum;
      }
    }
  }
}

template <typename scalar_t>
struct RenormRowsKernelFunctor : public __SYCL_KER_CONFIG_CONVENTION__ {
  void operator()(sycl::nd_item<1> item) const {
    renormRowsL1<scalar_t>(
        item,
        t_ptr,
        rows,
        cols,
        (unsigned char*)(slm_.template get_multi_ptr<
                                 sycl::access::decorated::no>()
                             .get()));
  }
  void sycl_ker_config_convention(sycl::handler& cgh) {
    slm_ = sycl_local_acc_t<scalar_t>(group_size_ / 8, cgh);
    // We use the smallest subgroup size to ensure enough space
  }
  RenormRowsKernelFunctor(
      int64_t rows_,
      int64_t cols_,
      scalar_t* t_ptr_,
      int group_size)
      : rows(rows_), cols(cols_), t_ptr(t_ptr_), group_size_(group_size) {}

 private:
  int64_t rows;
  int64_t cols;
  scalar_t* t_ptr;
  int group_size_;
  sycl_local_acc_t<scalar_t> slm_;
};

inline void renormRows(Tensor& t) {
  TORCH_CHECK(t.dim() == 2);
  int64_t rows = t.size(0);
  int64_t cols = t.size(1);

  int group_size = syclMaxWorkItemsPerEU();
  int num_groups = (rows + group_size - 1) / group_size;
  int hw_max_groups = syclMaxWorkItemsPerTile() / group_size;
  num_groups = num_groups > hw_max_groups ? hw_max_groups : num_groups;

  auto& sycl_queue = at::xpu::getCurrentSYCLQueue();
  AT_DISPATCH_FLOATING_TYPES_AND2(
      at::ScalarType::BFloat16,
      at::ScalarType::Half,
      t.scalar_type(),
      "renormRows_xpu",
      [&] {
        auto t_ptr = t.data_ptr<scalar_t>();
        auto kfn =
            RenormRowsKernelFunctor<scalar_t>(rows, cols, t_ptr, group_size);
        sycl_kernel_submit(
            num_groups * group_size, group_size, sycl_queue, kfn);
      });
}

template <typename scalar_t>
inline int binarySearchForMultinomial(
    scalar_t* cumdist,
    scalar_t* dist,
    int size,
    scalar_t val) {
  int start = 0;
  int end = size;
  // cumdist[size - 1] = 0 => all zero prob dist

  while (end - start > 0) {
    int mid = start + (end - start) / 2;

    scalar_t midVal = cumdist[mid];
    if (midVal < val) {
      start = mid + 1;
    } else {
      end = mid;
    }
  }

  if (start == size) {
    // No probability mass or precision problems; just return the
    // first non-zero element by setting start to size-1 here,
    // the code below will move it to the last non-zero probability
    // this actually can happen when the random number is 1
    // (github pytorch issue #4858).
    start = size - 1;
  }

  while (start >= 1 && dist[start] == 0)
    start--;

  return start;
}

template <typename scalar_t, typename item_t>
inline void sampleMultinomialWithReplacement(
    item_t& item,
    PhiloxState philox_args,
    int totalSamples,
    int64_t* dest,
    int64_t distributions,
    int categories,
    scalar_t* normDistPrefixSum,
    scalar_t* normDist) {
  auto thread_idx = item.get_local_id(1);
  auto thread_range = item.get_local_range(1);
  auto group_idx_x = item.get_group(1);
  auto group_idx_y = item.get_group(0);
  auto group_range_x = item.get_group_range(1);
  auto group_range_y = item.get_group_range(0);

  // At the moment, each subgroup computes one sample value in the binary
  // search due to divergence. It seems possible to compute multiple
  // values and limit divergence though later on.

  auto seeds = philox_unpack(philox_args);

  // global index formula for 2D grid of 1D group
  int idx = group_idx_y * group_range_x * thread_range +
      group_idx_x * thread_range + thread_idx;

  randStatePhilox4_32_10_t state;
  rand_init(std::get<0>(seeds), idx, std::get<1>(seeds), &state);

  // The block determines the distribution for which we generate a point
  for (int64_t curDist = group_idx_y; curDist < distributions;
       curDist += group_range_y) {
    for (int sample = group_idx_x * thread_range + thread_idx;
         sample < totalSamples;
         sample += thread_range * group_range_x) {
      // we are losing 3 out of 4 generated numbers but it's ok
      // this kernel is not very efficient anyway
      auto rand = rand_uniform4(&state);
      scalar_t r = static_cast<scalar_t>(rand.x);

      // Find the bucket that a uniform sample lies in
      int choice = binarySearchForMultinomial<scalar_t>(
          normDistPrefixSum + curDist * categories,
          normDist + curDist * categories,
          categories,
          r);

      dest[curDist * totalSamples + sample] = choice;
    }
  }
}

template <typename scalar_t>
struct MultinomialWithReplacementKernelImplFunctor {
  void operator()(sycl::nd_item<2> item) const {
    sampleMultinomialWithReplacement(
        item,
        rng_engine_inputs,
        n_sample,
        result_ptr,
        numDist,
        numCategories,
        prefixSum_ptr,
        normDist_ptr);
  }
  MultinomialWithReplacementKernelImplFunctor(
      PhiloxState rng_engine_inputs_,
      const int64_t n_sample_,
      int64_t* result_ptr_,
      int64_t numDist_,
      int numCategories_,
      scalar_t* prefixSum_ptr_,
      scalar_t* normDist_ptr_)
      : rng_engine_inputs(rng_engine_inputs_),
        n_sample(n_sample_),
        result_ptr(result_ptr_),
        numDist(numDist_),
        numCategories(numCategories_),
        prefixSum_ptr(prefixSum_ptr_),
        normDist_ptr(normDist_ptr_) {}

 private:
  PhiloxState rng_engine_inputs;
  const int64_t n_sample;
  int64_t* result_ptr;
  int64_t numDist;
  int numCategories;
  scalar_t* prefixSum_ptr;
  scalar_t* normDist_ptr;
};

void multinomial_with_replacement_kernel(
    Tensor& result,
    const Tensor& self,
    const int64_t n_sample,
    c10::optional<Generator> generator) {
  auto& sycl_queue = at::xpu::getCurrentSYCLQueue();
  auto gen = get_generator_or_default<at::XPUGeneratorImpl>(
      generator, at::xpu::detail::getDefaultXPUGenerator());

  int inputSize = self.dim();
  int64_t numDist = inputSize == 1 ? 1 : self.size(0);
  int numCategories = inputSize == 1 ? self.size(0) : self.size(1);

  // Restructure data for 2d
  auto self_v = inputSize == 1 ? self.view({numDist, numCategories}) : self;

  result.resize_({numDist, n_sample});

  AT_DISPATCH_FLOATING_TYPES_AND2(
      at::ScalarType::BFloat16,
      at::ScalarType::Half,
      self_v.scalar_type(),
      "multinomial_kernel_xpu",
      [&] {
        using accscalar_t = acc_type<scalar_t, true>;

        Tensor origDist = at::empty_like(self_v);
        origDist.copy_(self_v);

        Tensor normDist = at::empty_like(self_v);

        Tensor prefixSum = at::empty_like(self_v);

        // Renorm along rows
        normDist.copy_(origDist);
        renormRows(normDist);

        // Prefix sum along rows
        at::cumsum_out(prefixSum, normDist, 1);
        int group_size = syclMaxWorkItemsPerEU();
        int group_range_y = numDist;
        int group_range_x = (n_sample - 1) / group_size + 1;

        std::pair<uint64_t, uint64_t> rng_engine_inputs_;
        {
          // See Note [Acquire lock when using random generators]
          std::lock_guard<std::mutex> lock(gen->mutex_);
          auto offset = ((numDist - 1) / group_range_y + 1) * 4;
          rng_engine_inputs_ = gen->philox_engine_inputs(offset);
        }
        auto rng_engine_inputs = PhiloxState(
            std::get<0>(rng_engine_inputs_), std::get<1>(rng_engine_inputs_));
        // Sample with replacement

        auto result_ptr = result.data_ptr<int64_t>();
        auto prefixSum_ptr = prefixSum.data_ptr<scalar_t>();
        auto normDist_ptr = normDist.data_ptr<scalar_t>();
        auto kfn = MultinomialWithReplacementKernelImplFunctor<scalar_t>(
            rng_engine_inputs,
            n_sample,
            result_ptr,
            numDist,
            numCategories,
            prefixSum_ptr,
            normDist_ptr);

        sycl_kernel_submit(
            sycl::range<2>(group_range_y, group_range_x * group_size),
            sycl::range<2>(1, group_size),
            sycl_queue,
            kfn);
      });

  if (inputSize == 1) {
    result.resize_({n_sample});
  }
}

} // namespace at::native::xpu
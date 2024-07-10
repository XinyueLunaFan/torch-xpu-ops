#include <ATen/ATen.h>
#include <ATen/core/Reduction.h>
#include <ATen/native/TensorIterator.h>
#include <ATen/native/xpu/sycl/Loops.h>
#include <comm/SYCLContext.h>

namespace at::native::xpu {

template <typename scalar_t>
struct BinaryCrossEntropyKernelFunctor {
  scalar_t operator()(scalar_t input_val, scalar_t target_val) const {
    const scalar_t zero = 0;
    const scalar_t one = 1;
    const scalar_t neg_100 = -100;

    SYCL_KERNEL_ASSERT(input_val >= zero && input_val <= one);
    SYCL_KERNEL_ASSERT(target_val >= zero && target_val <= one);

    scalar_t log_input_val = std::log(input_val);
    scalar_t log_1_minus_input_val = std::log1p(-input_val);

    log_input_val = std::max(log_input_val, neg_100);
    log_1_minus_input_val = std::max(log_1_minus_input_val, neg_100);

    return ((target_val - one) * log_1_minus_input_val) -
        (target_val * log_input_val);
  }
};

Tensor& binary_cross_entropy_out_kernel(
    const Tensor& input,
    const Tensor& target,
    const std::optional<Tensor>& weight_opt,
    int64_t reduction,
    Tensor& loss) {
  c10::MaybeOwned<Tensor> weight_maybe_owned =
      at::borrow_from_optional_tensor(weight_opt);
  const Tensor& weight = *weight_maybe_owned;

  Tensor loss_squeezed = at::squeeze(loss);

  TensorIterator iter = TensorIteratorConfig()
                            .add_output(loss_squeezed)
                            .add_owned_input(at::squeeze(input))
                            .add_owned_input(at::squeeze(target))
                            .build();
  AT_DISPATCH_FLOATING_TYPES_AND2(
      at::ScalarType::Half,
      at::ScalarType::BFloat16,
      iter.common_dtype(),
      "binary_cross_entropy_out_xpu",
      [&]() { gpu_kernel(iter, BinaryCrossEntropyKernelFunctor<scalar_t>()); });
  if (weight.defined()) {
    loss.mul_(weight);
  }

  if (reduction != at::Reduction::None) {
    Tensor loss_reduced;
    if (reduction == at::Reduction::Mean) {
      loss_reduced = loss.mean();
    } else if (reduction == at::Reduction::Sum) {
      loss_reduced = loss.sum();
    }
    loss.resize_as_(loss_reduced).copy_(loss_reduced);
  }

  return loss;
}

Tensor binary_cross_entropy_kernel(
    const Tensor& input,
    const Tensor& target,
    const std::optional<Tensor>& weight_opt,
    int64_t reduction) {
  c10::MaybeOwned<Tensor> weight_maybe_owned =
      at::borrow_from_optional_tensor(weight_opt);
  const Tensor& weight = *weight_maybe_owned;
  Tensor loss = at::empty_like(input);
  return binary_cross_entropy_out_kernel(
      input, target, weight, reduction, loss);
}

template <typename scalar_t>
struct BinaryCrossEntropyBackwardKernelFunctor {
  scalar_t operator()(
      scalar_t grad_val,
      scalar_t input_val,
      scalar_t target_val) const {
    constexpr float EPSILON = 1e-12;
    const scalar_t one = 1;
    const scalar_t epsilon = EPSILON;

    scalar_t grad_input_denominator =
        std::max((one - input_val) * input_val, epsilon);

    return grad_val * (input_val - target_val) / grad_input_denominator;
  }
};

void binary_cross_entropy_backward_out_kernel_impl(
    Tensor& grad_input,
    const Tensor& grad,
    const Tensor& input,
    const Tensor& target) {
  at::TensorIterator iter = TensorIteratorConfig()
                                .add_output(grad_input)
                                .add_input(grad)
                                .add_input(input)
                                .add_input(target)
                                .build();
  AT_DISPATCH_FLOATING_TYPES_AND2(
      at::ScalarType::Half,
      at::ScalarType::BFloat16,
      iter.common_dtype(),
      "binary_cross_entropy_backward_out_xpu",
      [&]() {
        gpu_kernel(iter, BinaryCrossEntropyBackwardKernelFunctor<scalar_t>());
      });
}

Tensor& binary_cross_entropy_backward_out_kernel(
    const Tensor& grad,
    const Tensor& input,
    const Tensor& target,
    const std::optional<Tensor>& weight_opt,
    int64_t reduction,
    Tensor& grad_input) {
  c10::MaybeOwned<Tensor> weight_maybe_owned =
      at::borrow_from_optional_tensor(weight_opt);
  const Tensor& weight = *weight_maybe_owned;

  Tensor grad_expand = grad.expand_as(input);
  binary_cross_entropy_backward_out_kernel_impl(
      grad_input, grad_expand, input, target);

  if (weight.defined()) {
    grad_input.mul_(weight);
  }
  if (reduction == at::Reduction::Mean) {
    grad_input.div_(input.numel());
  }
  return grad_input;
}

Tensor binary_cross_entropy_backward_kernel(
    const Tensor& grad,
    const Tensor& input,
    const Tensor& target,
    const std::optional<Tensor>& weight_opt,
    int64_t reduction) {
  c10::MaybeOwned<Tensor> weight_maybe_owned =
      at::borrow_from_optional_tensor(weight_opt);
  const Tensor& weight = *weight_maybe_owned;
  Tensor grad_input = at::empty_like(input);
  return binary_cross_entropy_backward_out_kernel(
      grad, input, target, weight, reduction, grad_input);
}

} // namespace at::native::xpu

#include <ATen/ATen.h>
#include <ATen/ExpandUtils.h>
#include <ATen/ScalarOps.h>
#include <ATen/core/Tensor.h>
#include <ATen/native/DispatchStub.h>
#include <ATen/native/RangeFactories.h>
#include <ATen/native/TensorIterator.h>
// #include <ATen/xpu/XPUNativeFunctions.h>
#include <ATen/native/xpu/sycl/RangeFactoriesKernel.h>
#include <torch/library.h>

namespace at {

namespace native {
// REGISTER_XPU_DISPATCH(arange_stub, xpu::arange_kernel);
}

// Tensor& XPUNativeFunctions::arange_out(
//     const Scalar& start,
//     const Scalar& end,
//     const Scalar& step,
//     Tensor& out) {
//   return at::native::xpu::arange_kernel(start, end, step, out);
// }

} // namespace at
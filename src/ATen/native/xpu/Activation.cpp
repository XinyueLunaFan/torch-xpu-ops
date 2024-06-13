#include <ATen/ScalarOps.h>
#include <ATen/core/Tensor.h>
#include <ATen/native/Activation.h>
#include <ATen/native/DispatchStub.h>
#include <ATen/native/TensorIterator.h>

#include <ATen/xpu/ops/gelu_backward_native.h>
#include <ATen/xpu/ops/gelu_native.h>

// #include <ATen/xpu/XPUNativeFunctions.h>
#include <ATen/native/xpu/sycl/ActivationGeluKernel.h>
#include <ATen/native/xpu/sycl/ActivationThresholdKernel.h>

namespace at {

namespace native {
REGISTER_XPU_DISPATCH(threshold_stub, xpu::threshold_kernel);
// REGISTER_XPU_DISPATCH(GeluKernel, xpu::gelu_kernel);
// REGISTER_XPU_DISPATCH(GeluBackwardKernel, xpu::gelu_backward_kernel);

TORCH_IMPL_FUNC(gelu_backward_out_xpu)
(const Tensor& /*grad*/,
 const Tensor& /*self*/,
 c10::string_view approximate,
 const Tensor& /*grad_input*/
) {
  xpu::gelu_backward_kernel(*this, approximate);
}

TORCH_IMPL_FUNC(gelu_out_xpu)
(const Tensor& /*self*/, c10::string_view approximate, const Tensor& /*result*/
) {
  xpu::gelu_kernel(*this, approximate);
}

} // namespace native

// Tensor XPUNativeFunctions::relu(const Tensor& self) {
//   TORCH_CHECK(
//       self.scalar_type() != at::kBool, "Boolean inputs not supported for
//       relu");
//   return at::clamp_min(self, 0);
// }

// Tensor& XPUNativeFunctions::relu_(Tensor& self) {
//   TORCH_CHECK(
//       self.scalar_type() != at::kBool, "Boolean inputs not supported for
//       relu");
//   return at::clamp_min_(self, 0);
// }

// Tensor& XPUNativeFunctions::relu_out(const Tensor& self, Tensor& out) {
//   TORCH_CHECK(
//       self.scalar_type() != at::kBool, "Boolean inputs not supported for
//       relu");
//   return at::clamp_min_out(out, self, 0);
// }

// TensorIterator threshold_meta(
//     const Tensor& self,
//     const Scalar& threshold,
//     const Scalar& value,
//     Tensor& out) {
//   TensorIterator iter;
//   iter.build(TensorIteratorConfig()
//                  .set_check_mem_overlap(
//                      false) // threshold is idempotent, so overlap is okay
//                  .add_output(out)
//                  .add_const_input(self)
//                  .add_const_input(self) // other
//                  .allow_cpu_scalars(true)
//                  .promote_inputs_to_common_dtype(true)
//                  .cast_common_dtype_to_outputs(true)
//                  .enforce_safe_casting_to_output(true));
//   return iter;
// }

// Tensor XPUNativeFunctions::threshold(
//     const Tensor& self,
//     const Scalar& threshold,
//     const Scalar& value) {
//   Tensor out;
//   auto iter = threshold_meta(self, threshold, value, out);
//   native::xpu::threshold_kernel(iter, threshold, value);
//   return iter.output();
// }

// Tensor& XPUNativeFunctions::threshold_(
//     Tensor& self,
//     const Scalar& threshold,
//     const Scalar& value) {
//   auto iter = threshold_meta(self, threshold, value, self);
//   native::xpu::threshold_kernel(iter, threshold, value);
//   return self;
// }

// Tensor& XPUNativeFunctions::threshold_out(
//     const Tensor& self,
//     const Scalar& threshold,
//     const Scalar& value,
//     Tensor& out) {
//   auto iter = threshold_meta(self, threshold, value, out);
//   native::xpu::threshold_kernel(iter, threshold, value);
//   return out;
// }

// TensorIterator threshold_backward_meta(
//     const Tensor& grad,
//     const Tensor& self,
//     const Scalar& threshold,
//     Tensor& gradInput) {
//   TensorIterator iter;
//   iter.build(TensorIteratorConfig()
//                  .set_check_mem_overlap(
//                      false) // threshold is idempotent, so overlap is okay
//                  .add_output(gradInput)
//                  .add_input(self)
//                  .add_input(grad) // other
//                  .allow_cpu_scalars(true)
//                  .promote_inputs_to_common_dtype(true)
//                  .cast_common_dtype_to_outputs(true)
//                  .enforce_safe_casting_to_output(true));
//   return iter;
// }

// Tensor XPUNativeFunctions::threshold_backward(
//     const Tensor& grad_output,
//     const Tensor& self,
//     const Scalar& threshold) {
//   Tensor grad_input;
//   auto iter = threshold_backward_meta(grad_output, self, threshold,
//   grad_input); native::xpu::threshold_kernel(iter, threshold, 0); return
//   iter.output();
// }

// Tensor& XPUNativeFunctions::threshold_backward_out(
//     const Tensor& grad_output,
//     const Tensor& self,
//     const Scalar& threshold,
//     Tensor& grad_input) {
//   auto iter = threshold_backward_meta(grad_output, self, threshold,
//   grad_input); native::xpu::threshold_kernel(iter, threshold, 0); return
//   grad_input;
// }

// Tensor XPUNativeFunctions::gelu(
//     const Tensor& self,
//     c10::string_view approximate) {
//   Tensor out;
//   auto iter = TensorIterator::unary_op(out, self);
//   native::xpu::gelu_kernel(iter, approximate);
//   return iter.output();
// }

// Tensor& XPUNativeFunctions::gelu_(Tensor& self, c10::string_view approximate)
// {
//   auto iter = TensorIterator::unary_op(self, self);
//   native::xpu::gelu_kernel(iter, approximate);
//   return self;
// }

// Tensor& XPUNativeFunctions::gelu_out(
//     const Tensor& self,
//     c10::string_view approximate,
//     Tensor& out) {
//   auto iter = TensorIterator::unary_op(out, self);
//   native::xpu::gelu_kernel(iter, approximate);
//   return out;
// }

// Tensor XPUNativeFunctions::gelu_backward(
//     const Tensor& grad_output,
//     const Tensor& self,
//     c10::string_view approximate) {
//   Tensor grad_input;
//   auto iter =
//       TensorIterator::borrowing_binary_op(grad_input, grad_output, self);
//   native::xpu::gelu_backward_kernel(iter, approximate);
//   return iter.output();
// }

// Tensor& XPUNativeFunctions::gelu_backward_out(
//     const Tensor& grad_output,
//     const Tensor& self,
//     c10::string_view approximate,
//     Tensor& grad_input) {
//   auto iter =
//       TensorIterator::borrowing_binary_op(grad_input, grad_output, self);
//   native::xpu::gelu_backward_kernel(iter, approximate);
//   return grad_input;
// }

} // namespace at
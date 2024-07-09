#pragma once

#include <ATen/core/TensorBase.h>

namespace at::native::xpu {

void cumprod_kernel_impl(const Tensor& result, const Tensor& self, int64_t dim);

} // namespace at::native::xpu
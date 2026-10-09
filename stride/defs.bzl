"""Public Stride build rules for Bazel."""

load("//stride/internal:stride_cc_library.bzl", _stride_cc_library = "stride_cc_library")
load("//stride/internal:toolchain.bzl", _stride_toolchain = "stride_toolchain", _StrideToolchainInfo = "StrideToolchainInfo")

# Main user-facing rules
stride_cc_library = _stride_cc_library
stride_library = _stride_cc_library  # Alias

# Toolchain rules
stride_toolchain = _stride_toolchain
StrideToolchainInfo = _StrideToolchainInfo

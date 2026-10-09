"""Stride compiler toolchain definition for Bazel."""

StrideToolchainInfo = provider(
    doc = "Information about how to invoke the Stride compiler driver (stridec).",
    fields = {
        "stridec": "Executable artifact for stridec compiler driver.",
        "default_copts": "List of default compiler options to pass to stridec.",
    },
)

def _stride_toolchain_impl(ctx):
    toolchain_info = platform_common.ToolchainInfo(
        stride = StrideToolchainInfo(
            stridec = ctx.executable.stridec,
            default_copts = ctx.attr.default_copts,
        ),
    )
    return [toolchain_info]

stride_toolchain = rule(
    implementation = _stride_toolchain_impl,
    attrs = {
        "stridec": attr.label(
            doc = "The stridec compiler executable.",
            mandatory = True,
            executable = True,
            cfg = "exec",
            allow_single_file = True,
        ),
        "default_copts": attr.string_list(
            doc = "Default options passed to stridec on every invocation.",
            default = [],
        ),
    },
    doc = "Defines a Stride compiler toolchain.",
)

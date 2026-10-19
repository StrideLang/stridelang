"""Implementation of stride_cc_library and stride_library Starlark rules."""

load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cpp_toolchain")

def _deduce_target_args(ctx, cc_toolchain):
    """Automatically deduces target triple and CPU from the active C++ toolchain."""
    args = []

    cpu = getattr(cc_toolchain, "cpu", "")
    target_gnu_name = getattr(cc_toolchain, "target_gnu_system_name", "")
    target_name_lower = (cpu + " " + target_gnu_name).lower()

    if "cortex-m0" in target_name_lower or "armv6-m" in target_name_lower or "rp2040" in target_name_lower:
        args.extend(["--target=thumbv6m-none-eabi", "-mcpu=cortex-m0plus"])
    elif "cortex-m33" in target_name_lower or "armv8-m" in target_name_lower or "rp2350" in target_name_lower:
        args.extend(["--target=thumbv8m.main-none-eabi", "-mcpu=cortex-m33"])
    elif "cortex-m7" in target_name_lower or "stm32h7" in target_name_lower:
        args.extend(["--target=thumbv7em-none-eabihf", "-mcpu=cortex-m7"])
    elif "cortex-m4" in target_name_lower or "armv7e-m" in target_name_lower or "stm32f4" in target_name_lower:
        args.extend(["--target=thumbv7em-none-eabi", "-mcpu=cortex-m4"])
    elif "cortex-m3" in target_name_lower or "armv7-m" in target_name_lower:
        args.extend(["--target=thumbv7m-none-eabi", "-mcpu=cortex-m3"])
    elif "riscv32" in target_name_lower or "rv32" in target_name_lower:
        args.extend(["--target=riscv32-unknown-elf"])
    elif "wasm32" in target_name_lower:
        args.extend(["--target=wasm32-unknown-wasi"])
    elif "arm" in target_name_lower:
        args.extend(["--target=arm-none-eabi"])

    return args

def _stride_cc_library_impl(ctx):
    cc_toolchain = find_cpp_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )

    # Resolve compiler from toolchain or fallback attribute
    if "//stride:toolchain_type" in ctx.toolchains:
        stridec = ctx.toolchains["//stride:toolchain_type"].stride.stridec
        default_copts = ctx.toolchains["//stride:toolchain_type"].stride.default_copts
    elif ctx.executable._compiler:
        stridec = ctx.executable._compiler
        default_copts = []
    else:
        fail("No Stride toolchain or compiler found.")

    target_args = _deduce_target_args(ctx, cc_toolchain)
    
    # Declare single unified output object file and header file
    obj_file = ctx.actions.declare_file(ctx.label.name + "_gen/" + ctx.label.name + ".o")
    hdr_file = ctx.actions.declare_file(ctx.label.name + "_gen/include/" + ctx.label.name + ".h")

    args = ctx.actions.args()
    args.add("-c")
    args.add_all(ctx.files.srcs)
    args.add("-o", obj_file)
    args.add("--emit-header=" + hdr_file.path)
    
    if ctx.attr.emit_all_functions:
        args.add("--emit-all-functions")

    args.add_all(target_args)
    args.add_all(default_copts)
    args.add_all(ctx.attr.copts)

    # Include search paths
    for inc in ctx.attr.includes:
        args.add("-I" + inc)

    ctx.actions.run(
        mnemonic = "StrideCompile",
        progress_message = "Compiling Stride library %s (%d files) -> %s" % (
            ctx.label.name, len(ctx.files.srcs), obj_file.basename
        ),
        executable = stridec,
        inputs = ctx.files.srcs,
        outputs = [obj_file, hdr_file],
        arguments = [args],
        env = {},
    )

    # Build CcInfo compilation context (exposes generated headers and include dirs)
    include_dirs = [hdr_file.dirname]
    compilation_context = cc_common.create_compilation_context(
        headers = depset([hdr_file]),
        includes = depset(include_dirs),
        quote_includes = depset(include_dirs),
        system_includes = depset(include_dirs),
    )

    # Build CcInfo linking context (links generated object files)
    lib_to_link = cc_common.create_library_to_link(
        actions = ctx.actions,
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        objects = [obj_file],
        pic_objects = [obj_file],
    )

    # Resolve platform link options
    platform_linkopts = []
    if not ctx.attr.no_default_libs:
        cpu = getattr(cc_toolchain, "cpu", "").lower()
        # If not bare-metal microcontroller, inject -lm for math operations
        if not ("cortex" in cpu or "armv" in cpu or "wasm" in cpu or "riscv" in cpu or "rp2040" in cpu):
            platform_linkopts.append("-lm")

    linker_input = cc_common.create_linker_input(
        owner = ctx.label,
        libraries = depset([lib_to_link]),
        user_link_flags = depset(platform_linkopts + ctx.attr.linkopts),
    )

    linking_context = cc_common.create_linking_context(
        linker_inputs = depset([linker_input]),
    )

    cc_info = CcInfo(
        compilation_context = compilation_context,
        linking_context = linking_context,
    )

    # Merge with any dependent CcInfo
    dep_cc_infos = [dep[CcInfo] for dep in ctx.attr.deps if CcInfo in dep]
    merged_cc_info = cc_common.merge_cc_infos(cc_infos = [cc_info] + dep_cc_infos)

    return [
        DefaultInfo(
            files = depset([obj_file, hdr_file]),
            runfiles = ctx.runfiles(files = [hdr_file]),
        ),
        merged_cc_info,
    ]

stride_cc_library = rule(
    implementation = _stride_cc_library_impl,
    attrs = {
        "srcs": attr.label_list(
            doc = "List of .stride source files to compile into the library.",
            allow_files = [".stride"],
            mandatory = True,
        ),
        "emit_all_functions": attr.bool(
            doc = "Forces code generation for all root-level standalone functions and modules.",
            default = False,
        ),
        "deps": attr.label_list(
            doc = "List of C/C++ or Stride dependency targets.",
            providers = [CcInfo],
            default = [],
        ),
        "copts": attr.string_list(
            doc = "Additional compiler flags passed to stridec.",
            default = [],
        ),
        "linkopts": attr.string_list(
            doc = "Additional linker flags passed to downstream binaries linking this library.",
            default = [],
        ),
        "no_default_libs": attr.bool(
            doc = "Disables automatic injection of default platform math/runtime libraries.",
            default = False,
        ),
        "includes": attr.string_list(
            doc = "List of include search paths to add.",
            default = [],
        ),
        "_compiler": attr.label(
            doc = "Default fallback compiler executable.",
            default = Label("//:stridec"),
            executable = True,
            cfg = "exec",
        ),
        "_cc_toolchain": attr.label(
            default = Label("@rules_cc//cc:current_cc_toolchain"),
        ),
    },
    toolchains = [
        "//stride:toolchain_type",
        "@rules_cc//cc:toolchain_type",
    ],
    fragments = ["cpp"],
    doc = """
    Compiles one or more Stride stream modules into a single native machine object file and C header,
    exporting standard CcInfo providers for direct consumption by cc_binary and cc_library.
    """,
)

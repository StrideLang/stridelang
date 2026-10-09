"""Implementation of stride_cc_library and stride_library Starlark rules."""

load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cpp_toolchain")
load("@rules_cc//cc:action_names.bzl", "C_COMPILE_ACTION_NAME")

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
    
    generated_objects = []
    generated_headers = []
    include_dirs = []

    for src in ctx.files.srcs:
        base_name = src.basename.rsplit(".", 1)[0]
        
        # Declare output object file and header file
        obj_file = ctx.actions.declare_file(ctx.label.name + "_gen/" + base_name + ".o")
        hdr_file = ctx.actions.declare_file(ctx.label.name + "_gen/include/" + base_name + ".h")

        args = ctx.actions.args()
        args.add("-c")
        args.add(src)
        args.add("-o", obj_file)
        args.add("--emit-header=" + hdr_file.path)
        args.add_all(target_args)
        args.add_all(default_copts)
        args.add_all(ctx.attr.copts)

        # Include search paths from dependencies
        for inc in ctx.attr.includes:
            args.add("-I" + inc)

        ctx.actions.run(
            mnemonic = "StrideCompile",
            progress_message = "Compiling Stride source %s -> %s" % (src.short_path, obj_file.basename),
            executable = stridec,
            inputs = [src],
            outputs = [obj_file, hdr_file],
            arguments = [args],
            env = {},
        )

        generated_objects.append(obj_file)
        generated_headers.append(hdr_file)
        if hdr_file.dirname not in include_dirs:
            include_dirs.append(hdr_file.dirname)

    # Build CcInfo compilation context (exposes generated headers and include dirs)
    compilation_context = cc_common.create_compilation_context(
        headers = depset(generated_headers),
        includes = depset(include_dirs),
        quote_includes = depset(include_dirs),
        system_includes = depset(include_dirs),
    )

    # Build CcInfo linking context (links generated object files)
    libraries_to_link = []
    for obj in generated_objects:
        lib_to_link = cc_common.create_library_to_link(
            actions = ctx.actions,
            feature_configuration = feature_configuration,
            cc_toolchain = cc_toolchain,
            objects = [obj],
            pic_objects = [obj],
        )
        libraries_to_link.append(lib_to_link)

    linker_input = cc_common.create_linker_input(
        owner = ctx.label,
        libraries = depset(libraries_to_link),
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
            files = depset(generated_objects + generated_headers),
            runfiles = ctx.runfiles(files = generated_headers),
        ),
        merged_cc_info,
    ]

stride_cc_library = rule(
    implementation = _stride_cc_library_impl,
    attrs = {
        "srcs": attr.label_list(
            doc = "List of .stride source files.",
            allow_files = [".stride"],
            mandatory = True,
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
    Compiles Stride stream modules into native machine object files and C headers,
    exporting standard CcInfo providers for direct consumption by cc_binary and cc_library.
    """,
)

-- GloamingForest: The Twilight Forest 4.8 for Minecraft Bedrock Edition (LeviLamina native mod).
-- Build through tools/build.ps1 (it takes a build throttle slot, rule R23).
-- Adapted from LiteLDev/Levitate v0.1.0 (LGPL-3.0).

add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

set_policy("package.requires_lock", true)

-- GloamingForest runs on both the dedicated server (BDS) and the client (local worlds).
-- The levilamina package and the levibuildscript rules read this option.
option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

option("tests")
    set_default(true)
    set_showmenu(true)
    set_description("Build the host-side unit test binary (gf-tests)")
option_end()

-- The LeviLamina line this build targets. Each supported line is a lip variant (26_51, 26_60)
-- with its own lock file in locks/ (tools/build.ps1 -LeviLamina copies it over xmake-requires.lock).
option("levilamina")
    set_default("26.60.6")
    set_showmenu(true)
    set_description("The LeviLamina version to build against, e.g. 26.60.6")
option_end()

local ll_version = get_config("levilamina") or "26.60.6"
local ll_major, ll_minor = ll_version:match("^(%d+)%.(%d+)%.")
if not ll_major then
    raise("--levilamina must be a major.minor.patch version, got " .. ll_version)
end
-- 26.51 -> 2651, 26.60 -> 2660. Source guards read it as GF_LL_VERSION.
local ll_code = tonumber(ll_major) * 100 + tonumber(ll_minor)

add_requires("levilamina " .. ll_version, {configs = {target_type = get_config("target_type")}})
add_requires("levibuildscript")
includes("tools/xmake/rules/linkrule.lua")
add_requires("nlohmann_json v3.12.0")
if has_config("tests") then
    add_requires("gtest")
end

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

local function gf_common_flags()
    set_languages("c++20")
    set_toolchains("clang-cl")
    add_defines("NOMINMAX", "UNICODE", "_HAS_CXX23=1")
    add_defines("GF_LL_VERSION=" .. ll_code)
    add_cxflags("/utf-8", "/W4")
    add_cxflags(
        "-Wno-microsoft-cast",
        "-Wno-invalid-offsetof",
        "-Wno-c++2b-extensions",
        "-Wno-microsoft-include",
        "-Wno-overloaded-virtual",
        "-Wno-ignored-qualifiers",
        "-Wno-missing-field-initializers",
        "-Wno-potentially-evaluated-expression",
        "-Wno-pragma-system-header-outside-header",
        {tools = {"clang_cl"}}
    )
end

-- Pure, game-independent logic (src/core). No mc/ll headers allowed here.
target("gf-core")
    set_kind("static")
    gf_common_flags()
    set_exceptions("none")
    add_cxflags("/EHa")
    add_packages("nlohmann_json", {public = true})
    add_includedirs("src", {public = true})
    add_headerfiles("src/core/**.h")
    add_files("src/core/**.cpp")

-- The mod itself. Every translation unit is linked directly into the DLL (not through a static
-- library) so that static self-registration objects (GF_REGISTER_*) are never stripped.
target("GloamingForest")
    add_rules("gf.linkrule")
    add_rules("@levibuildscript/modpacker")
    set_kind("shared")
    -- Prelink gets the objects through one library (tools/xmake/rules/linkrule.lua); short object
    -- paths still keep the final link's command line small.
    set_objectdir("$(builddir)/o")
    set_policy("build.intermediate_directory", false)
    gf_common_flags()
    set_exceptions("none") -- To avoid conflicts with /EHa.
    add_cxflags("/EHa", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
    add_cxflags("/EHs", {tools = {"clang_cl"}})
    set_symbols("debug")
    add_deps("gf-core")
    add_packages("levilamina", "nlohmann_json")
    add_includedirs("src")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp|core/**.cpp")
    if is_config("target_type", "client") then
        add_defines("GF_CLIENT=1")
    else
        add_defines("GF_SERVER=1")
    end
    -- Assemble bin/<target_type>/GloamingForest/{dll,pdb,manifest.json,lang/,packs/}.
    after_build(function (target)
        import("core.project.config")
        import("assemble", {rootdir = path.join(os.projectdir(), "tools", "xmake")})
        assemble(target, config.get("target_type") or "server")
    end)

if has_config("tests") then
    target("gf-tests")
        set_kind("binary")
        set_default(false)
        gf_common_flags()
        set_exceptions("cxx")
        add_deps("gf-core")
        add_packages("gtest", "nlohmann_json")
        add_files("tests/**.cpp")
        add_tests("default")
end

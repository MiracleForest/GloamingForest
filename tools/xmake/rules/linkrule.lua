-- Adapted from LiteLDev/levibuildscript 0.6.1 (rules/linkrule.lua), LGPL-3.0-or-later.
-- Same prelink step, but the object files reach prelink.exe through one static library built from a
-- response file: with every unit merged, the object list alone passes Windows' 32767-character
-- command-line limit (prelink has no response-file option).
rule("gf.linkrule")
    on_config(function(target)
        target:add("shflags", "/DELAYLOAD:bedrock_runtime.dll")
    end)

    before_link(function(target)
        print("Running prelink...")

        import("lib.detect.find_file")
        import("lib.detect.find_program")
        import("core.project.config")

        local plat = config.get("plat") or "windows"
        local arch = config.get("arch") or "x64"
        local target_type = config.get("target_type") or "server"

        local builddir = config.builddir()
        local outdir = path.join(builddir, ".prelink")
        local libdir = path.join(outdir, "lib")

        os.mkdir(libdir)

        local envs = os.joinenvs(target:pkgenvs(), os.getenvs())
        local searchpaths = path.splitenv(envs.PATH or envs.Path or envs.path or "")
        local data = assert(find_file("bedrock_runtime_data", searchpaths), "Cannot find bedrock_runtime_data")
        local link = assert(find_file("prelink.exe", searchpaths), "Cannot find prelink.exe")

        -- Pack the objects into one library (llvm-lib reads a response file).
        local ar = find_program("llvm-lib", {paths = searchpaths}) or find_program("lib", {paths = searchpaths})
        assert(ar, "Cannot find llvm-lib or lib")
        local objlib = path.join(outdir, "objects.lib")
        local rsp = path.join(outdir, "objects.rsp")
        local lines = {"/OUT:" .. path.absolute(objlib)}
        for _, obj in ipairs(target:objectfiles()) do
            table.insert(lines, '"' .. path.absolute(obj) .. '"')
        end
        io.writefile(rsp, table.concat(lines, "\n") .. "\n")
        os.tryrm(objlib)
        os.execv(ar, {"/NOLOGO", "@" .. path.absolute(rsp)})

        local inputs = {objlib}
        for _, dep in ipairs(target:orderdeps()) do
            if dep:kind() == "static" then
                local libfile = dep:targetfile()
                if libfile and os.isfile(libfile) then
                    table.insert(inputs, libfile)
                end
            end
        end
        for _, pkg in ipairs(target:orderpkgs()) do
            for _, linkdir in ipairs(pkg:get("linkdirs")) do
                for _, l in pairs(pkg:get("links")) do
                    local libfile = path.join(linkdir, l .. ".lib")
                    if os.isfile(libfile) then
                        table.insert(inputs, libfile)
                    end
                end
            end
        end

        os.execv(link, {
            string.format("%s-%s-%s", target_type, plat, arch),
            outdir,
            data,
            table.unpack(inputs)
        })

        target:add("linkdirs", libdir)
        target:add("links", "bedrock_runtime_api")
    end)

    after_link(function(target)
        os.rm("$(builddir)/.prelink/lib/*.lib")
    end)
rule_end()

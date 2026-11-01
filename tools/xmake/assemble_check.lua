-- `xmake lua tools/xmake/assemble_check.lua <projectdir> <outdir>` (tools/assemble.ps1 -Check):
-- assembles packs, lang and data into <outdir> and runs every check, without building the DLL.
function main(projectdir, outdir)
    import("assemble", {rootdir = path.join(projectdir, "tools", "xmake")})
    assemble.check(projectdir, outdir)
end
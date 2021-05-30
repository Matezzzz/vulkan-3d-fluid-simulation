import pathlib
import os
from subprocess import run
import colorama
import sys

colorama.init()

root_dir = pathlib.Path(".")

return_code = 0
did_something = False

for shader_dir in root_dir.iterdir():
    if shader_dir.is_dir():
        shaders = [ s for s in shader_dir.iterdir()]
        for shader in shaders:
            name, shader_type = shader.name.split(".")
            if shader_type != "spv":
                compiled_shader = [s for s in shaders if shader_type == s.name.split(".")[0]]
                compiled_shader = compiled_shader[0] if len(compiled_shader) else None
                if not compiled_shader or os.stat(compiled_shader).st_mtime < os.stat(shader).st_mtime:
                    did_something = True
                    if not compiled_shader: compiled_shader = str(shader_dir) + "/" + shader_type + ".spv"
                    print (colorama.Style.BRIGHT, colorama.Fore.GREEN, "Compiling ", shader, sep="")
                    proc = run(["glslangValidator", "-V", shader, "-o", compiled_shader], capture_output=True)
                    if proc.returncode:
                        print (colorama.Style.RESET_ALL, "Output: ", colorama.Style.BRIGHT, colorama.Fore.RED, proc.stdout.decode("ascii"), sep="")
                        return_code = 1
if not did_something:
    print (colorama.Style.BRIGHT, colorama.Fore.GREEN, "All shaders up to date, nothing to be done.", colorama.Style.RESET_ALL)
sys.exit(return_code)
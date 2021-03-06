# darktable which sucks less

this is an experimental complete rewrite of darktable, naturally at this point
with a heavily reduced feature set.

# build instructions

you should have checked out this repo recursively. if not, do
```
git submodule init
git submodule update
```
to grab the dependencies in the ext/ folder. you should then
be able to simply run 'make' from the bin/ folder. for
debug builds (which enable the vulkan validation layers), try

```
cd bin/
make debug -j12
```

please see the 'bin/run.sh' shell scripts for notes on my current
setup using the vulkan sdk instead of what your system ships, if
you want to run that.

# running

the binaries are currently wired to run from the bin/ directory:
```
cd bin/
./vkdt -d all /path/to/your/rawfile.raw
```
note that you have to edit the filename in the example cfg to point to a file
that actually exists on your system. if you run the command line interface
'vkdt-cli', it will replace all 'display' nodes by 'export' nodes.

# licence

our code is licenced under the 2-clause bsd licence (if not clearly marked
otherwise in the respective source files). there are parts from other libraries
that are licenced differently. in particular:

rawspeed:     LGPLv2
pthread-pool: LGPLv2
imgui:        MIT

and we may link to some others, too.

# dependencies:
- vulkan, glslangValidator
- sdl2
- submodule imgui
- submodule rawspeed (pulls in pugixml, stdc++, zlib, jpeg)
- submodule pybind11?
- libjpeg
- build: make, sed


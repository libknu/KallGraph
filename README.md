# KallGraph

KallGraph is based on LLVM & SVF, first we need to build and set LLVM/SVF properly.

We use [LLVM 14.0.6](https://github.com/llvm/llvm-project/releases/tag/llvmorg-14.0.6), to build, follow the instructions:
https://releases.llvm.org/14.0.0/docs/CMake.html
For better performance, build with -DCMAKE_BUILD_TYPE=Release

We use SVF-2.5 (included a patched version), following the command:
```
cd SVF-2.5
(Specify your LLVM-14 path by set(ENV{LLVM_DIR} /path/to/your/llvm-14.0.6.build/lib/cmake) in the root CMakeLists.txt)
source ./build.sh
cd ..
```

Before running KallGraph, make sure you have compiled target programs' LLVM IRs, and put those IR paths in a file like src/sample_input/bc.list, we give an example as follows to use MLTA IRDumper to build linux-6.5 IRs.

[MLTA](https://github.com/umnsec/mlta) provides a decent tool to compile LLVM IRs for Linux kernels, to use them, following commands:
```
git clone https://github.com/umnsec/mlta.git
cd mlta/IRDumper
(In the Makefile, change the LLVM_BUILD to /path/to/your/llvm-14.0.6.build)
make
cd ..
(Consider replacing MLTA's irgen with our modified irgen.sh, and setup paths correctly)
chmod +x irgen.sh
./irgen.sh
```

Since we are compiling LLVM IRs not binaries, there will be lots of compilation errors for binaries, but it won't effect the output of IRs.

To get the bc.list under the folder of linux source code:
```
find ./ -name "*.bc" ! -name "*timeconst.bc" > bc.list
```

Later on, just use the path to this bc.list as one of the input of KallGraph.

Now we have both LLVM-14 and SVF-2.5, please set them properly also in the root CMakeLists.txt of KallGraph.

Build KallGraph:

```
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j 4
cd ..
```

Run KallGraph (don't forget to use symbol '@' before the /path/to/bc.list):
```
build/bin/KallGraph @src/sample_input/bc.list -OutputDir=src/sample_output/ -ThreadNum=64
```

The output callgraph will present as /path/to/OutputDir/callgraph

https://www.computer.org/csdl/proceedings-article/sp/2025/223600c734

```
@inproceedings{li2025redefining,
  title={Redefining Indirect Call Analysis with KallGraph},
  author={Li, Guoren and Sridharan, Manu and Qian, Zhiyun},
  booktitle={2025 IEEE Symposium on Security and Privacy (SP)},
  pages={2734--2752},
  year={2025},
  organization={IEEE Computer Society}
}
```
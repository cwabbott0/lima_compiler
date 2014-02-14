Lima Shader Compiler
====================

This is a project to integrate the compiler backends that were being developed in
[open-gpu-tools](https://gitorious.org/open-gpu-tools/cwabbotts-open-gpu-tools) with the
Mesa shader compiler. Eventually, there will be both a near drop-in replacement for the
[Mali offline shader compiler](http://malideveloper.arm.com/develop-for-mali/tools/analysis-debug/mali-gpu-offline-shader-compiler/)
(for Mali-200 and Mali-400 only for now...) and a LD_PRELOAD'able library that can
replace the internal shader compiler in the Mali drivers for ES 2.0 apps, for testing and
comparison purposes.

In order to get a standalone version of Mesa's GLSL IR, this is based on the
[glsl-optimizer project](https://github.com/aras-p/glsl-optimizer).

Building
--------

### Linux and OSX

to build the stanalone compiler, in the root directory run:

    make standalone

and for the LD_PRELOAD'able library:

    make lib

or just run "make" to build both.

## Windows

Have fun.

Dev Notes
---------

Pulling Mesa upstream:

    git fetch upstream
    git merge upstream/master
    sh removeDeletedByUs.sh
    # inspect files, git rm unneeded ones, fix conflicts etc.
    # git commit
    
Rebuilding flex/bison parsers:

* When .y/.l files are changed, the parsers are *not* rebuilt automatically,
* Run ./generateParsers.sh to do that. You'll need bison & flex (on Mac, do "Install Command Line Tools" from Xcode)
* I use bison 2.3 and flex 2.5.35 (in OS X 10.8/10.9)


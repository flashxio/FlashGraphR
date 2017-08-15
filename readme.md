FlashGraphR is an R package that exposes a collection of graph algorithms
written on top of FlashGraph. By utilizing solid-state drives, this R package
is able to run these graph algorithms on a graph with billions of vertices
and hundreds of billions of edges in a single machine.

## Installation

To install from Github directly, 
```
git clone --recursive https://github.com/flashxio/FlashGraphR.git
cd FlashGraphR
R CMD build .
R CMD INSTALL FlashGraphR_0.1-0.tar.gz
```

To install from the tar file directly,
```
R -e "install.packages("https://github.com/flashxio/FlashGraphR/releases/download/FlashGraphR-latest/FlashGraphR.tar.gz", repos=NULL)"
```
However, the tar file may contain a less up-to-date version.

**Note: FlashGraphR relies on some Linux packages.** Please follow the instructions
[here](https://flashxio.github.io/FlashX-doc/FlashX-Quick-Start-Guide.html)
for more details of installing the Linux packages and FlashGraphR.

## Documentation

Please visit http://flashx.io/.

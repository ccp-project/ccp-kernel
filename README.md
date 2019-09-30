# CCP Kernel Datapath

This repository is a Linux kernel module that allows the Linux kernel to be used as a CCP datapath.

For instructions on building this module and running CCP, 
please see our [guide](https://ccp-project.github.io/ccp-guide).


## Notes

- We support Linux kernel versions 4.19+ (i.e., after [our patch](https://patchwork.ozlabs.org/patch/941532/)).
- For kernel 4.13 and 4.14, we additionally provide a compatibility mode to allow use with unmodified kernels.
- For other kernel versions, we suggest manually applying the patch and compiling your own kernel.

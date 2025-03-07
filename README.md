# CCP Kernel Datapath

This repository is a Linux kernel module that allows the Linux kernel to be used as a CCP datapath.

For instructions on building this module and running CCP, 
please see our [guide](https://ccp-project.github.io/ccp-guide).

[portus](https://github.com/ccp-project/portus), the CCP algorithm runtime library, in version 0.6 changed the libccp API slightly: the datapath now sends a `ready` message in `ccp_init`. This means that algorithms built on portus 0.5 or earlier won't work with the latest commits in this repository. The `portus-0.5-compat` branch should work in those cases.
As of portus 0.6.1, a compatibility mode is added automatically, but using the non-ready-message way will incur a slight delay on connection creation.

## Notes

- We support Linux kernel versions 4.19+ (i.e., after [our patch](https://patchwork.ozlabs.org/patch/941532/)). The current recommended kernel version is 6.9.
- For other kernel versions, we suggest manually applying the patch and compiling your own kernel.

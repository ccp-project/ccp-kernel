# CCP Kernel Datapath

This repository is a Linux kernel module that allows the Linux kernel to be used as a CCP datapath.

For instructions on building this module and running CCP, 
please see our [guide](https://ccp-project.github.io/ccp-guide).

[portus](https://github.com/ccp-project/portus), the CCP algorithm runtime library, in version 0.6 changed the libccp API slightly: the datapath now sends a `ready` message in `ccp_init`. This means that algorithms built on portus 0.5 or earlier won't work with the latest commits in this repository. The `portus-0.5-compat` branch should work in those cases.
As of portus 0.6.1, a compatibility mode is added automatically, but using the non-ready-message way will incur a slight delay on connection creation.

## Linux kernel version support

- We support Linux kernel version 6.10+, as of [this commit](https://github.com/ccp-project/ccp-kernel/commit/981384a51ae731b9381bc42a88567c1f61f8824b).
- If you need to run on Linux kernel versions between 4.19 and 6.9, revert the above commit and it should work.
- Kernels older than 4.19 (i.e., before [our patch](https://patchwork.ozlabs.org/patch/941532/)) are not supported.

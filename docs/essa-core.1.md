% essa-core(1)
% EssaBlockChain Development Foundation
%

# NAME

essa-core - Core daemon for EssaBlockChain payment network

# SYNOPSYS

essa-core [OPTIONS]

# DESCRIPTION

EssaBlockChain is a decentralized, federated peer-to-peer network that allows
people to send payments in any asset anywhere in the world
instantaneously, and with minimal fee. `EssaBlockChain-core` is the core
component of this network. `EssaBlockChain-core` is a C++ implementation of
the EssaBlockChain Consensus Protocol configured to construct a chain of
ledgers that are guaranteed to be in agreement across all the
participating nodes at all times.

## Configuration file

In most modes of operation, essa-core requires a configuration
file.  By default, it looks for a file called `essa-core.cfg` in
the current working directory, but this default can be changed by the
`--conf` command-line option.  The configuration file is in TOML
syntax.  The full set of supported directives can be found in
`%prefix%/share/doc/essa-core_example.cfg`.

%commands%

# EXAMPLES

See `%prefix%/share/doc/*.cfg` for some example essa-core
configuration files

# FILES

essa-core.cfg
:   Configuration file (in current working directory by default)

# SEE ALSO

<https://www.essa.org/developers/essa-core/software/admin.html>
:   essa-core administration guide

<https://www.essa.org>
:   Home page of EssaBlockChain development foundation

# BUGS

Please report bugs using the github issue tracker:\
<https://github.com/essa/essa-core/issues>

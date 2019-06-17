#pragma once

// Copyright 2018 EssaBlockChain Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <string>

namespace essa
{

class Config;

void checkQuorumIntersection(Config cfg);
void inferQuorumAndWrite(Config cfg);
void writeQuorumGraph(Config cfg, std::string const& outputFile);
}

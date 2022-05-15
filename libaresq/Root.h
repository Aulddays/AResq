#pragma once

#include <vector>
#include "record.h"

class Root
{
public:
	Root();
	~Root();

private:
	std::vector<RecordItem> record;
	std::vector<HistItem> hist;
};


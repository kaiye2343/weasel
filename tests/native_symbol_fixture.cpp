struct FixtureRecord { int value; int extra; };
__declspec(noinline) int SymbolFixtureAdd(const FixtureRecord& item) { return item.value+item.extra; }
int main(){FixtureRecord record{3,4};return SymbolFixtureAdd(record)==7?0:1;}

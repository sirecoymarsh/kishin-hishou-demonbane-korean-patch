#pragma once
// Display identity only. Native AI IDs and actor memory are never changed.
static int DhnIndex(bool duel,int group,const char* definition,int original)
{
 if(!duel || group!=1 || !definition)return original;
 const char* family[]={"bm","dm","gm","lm","mm","pm","km","im","nm","jm","qm","fm","rm","cm","sm","am","em","hm"};
 const int label[]={0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
 for(int i=0;i<18;++i)if(NcDefinitionFamily(definition,family[i]))return label[i];
 return original;
}
struct DhnGlyph {char c;int x,y,w;};
static const DhnGlyph dhnGlyphs[]={
 {'D',321,141,9},{'E',331,141,6},{'M',339,141,11},{'O',351,141,9},
 {'N',361,141,9},{'B',371,141,8},{'A',380,141,9},{'R',396,1,8},
 {'S',438,1,7},{'I',375,1,3},{'U',417,85,8}
};
static const char* DhnText(int index)
{
 return index==14?"DEMONBANE":index==15?"AEON":index==16?"AMBROSIUS":0;
}
static const DhnGlyph* DhnGlyphFor(char c)
{
 for(unsigned i=0;i<sizeof(dhnGlyphs)/sizeof(dhnGlyphs[0]);++i)if(dhnGlyphs[i].c==c)return &dhnGlyphs[i];
 return 0;
}
static int DhnTextWidth(const char* s)
{
 if(!s)return 0;int n=0;
 for(;*s;++s){const DhnGlyph* g=DhnGlyphFor(*s);if(!g)return -1;n+=g->w+1;}
 return n?n-1:0;
}

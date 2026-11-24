#pragma once
enum NativeCommandFamily { NC_NONE, NC_KRAKEN, NC_BYAKHEE };
static NativeCommandFamily NativeCommandScope(bool balanced,bool automatic,int tier,int source,int loaded)
{
    if(!balanced || !automatic || tier!=1)return NC_NONE;
    if(source==506 && loaded==7329)return NC_KRAKEN;
    if(source==522 && loaded==5923)return NC_BYAKHEE;
    return NC_NONE;
}

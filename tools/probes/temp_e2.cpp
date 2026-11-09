#include "core/CacheManager.h"
#include "core/SlimeEngine.h"
#include <cstdio>
#include <random>
int main(int argc,char**argv){
  if(argc<2){std::printf("usage: e2 <cache>\n");return 1;}
  slime::CacheManager c; std::string err;
  if(!c.loadFile(argv[1],true,err)){std::printf("load fail %s\n",err.c_str());return 2;}
  int minX=0,maxX=0,minZ=0,maxZ=0; bool first=true;
  for(const slime::Rect&r:c.ranges()){ if(first){minX=r.minX;maxX=r.maxX;minZ=r.minZ;maxZ=r.maxZ;first=false;}
    else{ if(r.minX<minX)minX=r.minX; if(r.maxX>maxX)maxX=r.maxX; if(r.minZ<minZ)minZ=r.minZ; if(r.maxZ>maxZ)maxZ=r.maxZ; } }
  std::mt19937_64 rng(7); long long checked=0,agree=0,cacheSlime=0,truthSlime=0,mismatchSlime=0;
  while(checked<10000){
    int x=minX+(int)(rng()%(unsigned)(maxX-minX+1)), z=minZ+(int)(rng()%(unsigned)(maxZ-minZ+1));
    int lk=c.lookup(x,z); if(lk<0) continue;
    bool truth=slime::SlimeEngine::isSlimeChunk(slime::Version::Bedrock,0,x,z);
    ++checked; if(lk==1)++cacheSlime; if(truth)++truthSlime;
    if((lk==1)==truth)++agree; else if(lk==1)++mismatchSlime;
  }
  std::printf("E2 checked=%lld agree=%lld (%.2f%%) cacheSlime=%lld truthSlime=%lld cacheOnlySlime=%lld\n",
              checked,agree,100.0*agree/checked,cacheSlime,truthSlime,mismatchSlime);
  return 0;
}
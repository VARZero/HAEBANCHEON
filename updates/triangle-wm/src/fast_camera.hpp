#pragma once
// Camera matrices for the same 48 x 36 degree output FOV. Q26 intermediates;
// only the nine pose basis coordinates need software floating-point arithmetic.
static std::array<int32_t,180> fast_camera(double yaw=0,double pitch=0){
 if(yaw==0&&pitch==0)return Geometry().camera();
 auto a=axes(yaw,pitch);V dx=a[1]*(2*std::tan(rad(24))/1280),dy=a[2]*(-2*std::tan(rad(18))/720),start=a[0]-dx*(1279./2)-dy*(719./2);
 std::array<V,3> vectors{start,dx,dy};int32_t q[3][3];for(int i=0;i<3;i++)for(int k=0;k<3;k++)q[i][k]=std::lround(vectors[i][k]*67108864);
 std::array<int32_t,180> result{};for(int f=0;f<20;f++)for(int row=0;row<3;row++)for(int col=0;col<3;col++){int64_t sum=0;for(int k=0;k<3;k++)sum+=int64_t(camera_inverse[f][row][k])*q[col][k];result[f*9+row*3+col]=sum>=0?(sum+(1LL<<27))>>28:-((-sum+(1LL<<27))>>28);}
 return result;
}

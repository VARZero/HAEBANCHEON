#include <cstdlib>
#include <cstring>
int float_server_main(int,char**);
int fixed_server_main(int,char**);
int matrix_app_main(int,char**);
int matrix_input_main(int,char**);
int main(int argc,char**argv){
 if(argc>1&&!std::strcmp(argv[1],"--app"))return matrix_app_main(argc-1,argv+1);
 if(argc>1&&!std::strcmp(argv[1],"--input"))return matrix_input_main(argc-1,argv+1);
 auto p=std::getenv("TRIANGLE_NUMERIC");
 return p&&!std::strcmp(p,"float")?float_server_main(argc,argv):fixed_server_main(argc,argv);
}

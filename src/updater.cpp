#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "updater.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <algorithm>
#include <vector>
#include <array>
#include <cstdio>
#include <cwctype>
#include "third_party/yyjson/yyjson.h"
namespace updates {
using S=std::wstring;
static const S Prefix=L"https://github.com/Phoenix1996/shuweibing/releases/download/";
static S ownPath(){std::vector<wchar_t> buf(32768);DWORD n=GetModuleFileNameW(nullptr,buf.data(),(DWORD)buf.size());return n&&n<buf.size()?S(buf.data(),n):S();}
static S fullPath(const S& path){std::vector<wchar_t> buf(32768);DWORD n=GetFullPathNameW(path.c_str(),(DWORD)buf.size(),buf.data(),nullptr);return n&&n<buf.size()?S(buf.data(),n):S();}
static bool samePath(const S& a,const S& b){return !a.empty()&&!b.empty()&&CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;}
static S quote(const S& value){S out=L"\"";int slashes=0;for(auto c:value){if(c==L'\\'){slashes++;continue;}out.append(slashes*(c==L'"'?2:1),L'\\');slashes=0;if(c==L'"')out+=L'\\';out+=c;}out.append(slashes*2,L'\\');return out+L'"';}
static S wide(const char* p,size_t n){if(!p||n>1024*1024)return {};int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p,(int)n,nullptr,0);if(!count)return {};S s(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p,(int)n,s.data(),count);if(s.find(L'\0')!=S::npos)return {};return s;}
static S str(yyjson_val* obj,const char* key){auto v=yyjson_obj_get(obj,key);return wide(yyjson_get_str(v),yyjson_get_len(v));}
static bool version(const S& text,std::array<int,3>& out){size_t at=0;for(int i=0;i<3;i++){size_t begin=at;int n=0;while(at<text.size()&&text[at]>=L'0'&&text[at]<=L'9'){if(at-begin>=6)return false;n=n*10+text[at++]-L'0';}if(at==begin)return false;out[i]=n;if(i<2){if(at>=text.size()||text[at++]!=L'.')return false;}}return at==text.size();}
static S fileVersion(const S& path){DWORD unused=0,n=GetFileVersionInfoSizeW(path.c_str(),&unused);if(!n||n>65536)return {};std::vector<unsigned char> bytes(n);if(!GetFileVersionInfoW(path.c_str(),0,n,bytes.data()))return {};wchar_t* value=nullptr;UINT length=0;if(!VerQueryValueW(bytes.data(),L"\\StringFileInfo\\040904B0\\ProductVersion",(void**)&value,&length)||!length)return {};return S(value,length-1);}
S currentVersion(){return fileVersion(ownPath());}
static bool parseRelease(const std::vector<unsigned char>& bytes,const S& current,Release& output){
    if(bytes.empty()||bytes.size()>1024*1024)return false;
    yyjson_doc* doc=yyjson_read((const char*)bytes.data(),bytes.size(),0);if(!doc)return false;
    auto root=yyjson_doc_get_root(doc);bool valid=yyjson_is_obj(root)&&!yyjson_get_bool(yyjson_obj_get(root,"draft"))&&!yyjson_get_bool(yyjson_obj_get(root,"prerelease"));
    S tag=str(root,"tag_name");std::array<int,3> next{},old{};
    valid=valid&&tag.size()>1&&tag[0]==L'v'&&version(tag.substr(1),next)&&version(current,old);
    Release candidate;if(valid&&next>old){candidate.version=tag.substr(1);candidate.body=str(root,"body");
        auto assets=yyjson_obj_get(root,"assets");size_t i,max;yyjson_val* asset;
        yyjson_arr_foreach(assets,i,max,asset){S name=str(asset,"name"),url=str(asset,"browser_download_url");
            if(name==L"ShuWeiBing.exe"&&url==Prefix+tag+L"/ShuWeiBing.exe"&&yyjson_get_uint(yyjson_obj_get(asset,"size"))>0&&yyjson_get_uint(yyjson_obj_get(asset,"size"))<=20*1024*1024)candidate.exeUrl=url;
            if(name==L"SHA256SUMS.txt"&&url==Prefix+tag+L"/SHA256SUMS.txt")candidate.sumUrl=url;
        }
        if(candidate.exeUrl.empty()||candidate.sumUrl.empty())valid=false;
    }
    yyjson_doc_free(doc);if(valid)output=std::move(candidate);return valid;
}
struct Internet {HINTERNET handle=nullptr;explicit Internet(HINTERNET h):handle(h){}~Internet(){if(handle)WinHttpCloseHandle(handle);}operator HINTERNET()const{return handle;}};
static bool get(const S& url,size_t limit,std::vector<unsigned char>& data,S& error,S* finalUrl=nullptr,DWORD* statusOut=nullptr){
    if(statusOut)*statusOut=0;
    URL_COMPONENTSW parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=(DWORD)-1;
    if(!WinHttpCrackUrl(url.c_str(),0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS){error=L"更新地址无效。";return false;}
    S host(parts.lpszHostName,parts.dwHostNameLength),path(parts.lpszUrlPath,parts.dwUrlPathLength);if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    if(host!=L"api.github.com"&&host!=L"github.com"){error=L"更新来源不是项目 GitHub。";return false;}
    Internet session(WinHttpOpen(L"ShuWeiBing",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));if(!session){error=L"无法初始化更新连接。";return false;}
    WinHttpSetTimeouts(session,10000,10000,15000,15000);
    Internet connection(WinHttpConnect(session,host.c_str(),parts.nPort,0));
    Internet request(WinHttpOpenRequest(connection,finalUrl?L"HEAD":L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    // HTTPS-only redirect policy retains GitHub's release-asset CDN support.
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
    const wchar_t* headers=host==L"api.github.com"?L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n":L"Accept: application/octet-stream\r\n";
    if(!request||!WinHttpSendRequest(request,headers,(DWORD)-1,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,nullptr)){error=L"无法连接 GitHub，请稍后重试（"+std::to_wstring(GetLastError())+L"）。";return false;}
    DWORD status=0,n=sizeof(status);bool queried=WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&n,WINHTTP_NO_HEADER_INDEX)!=0;if(statusOut)*statusOut=status;
    if(!queried||status!=200){error=L"GitHub 暂时不可用（"+std::to_wstring(status)+L"）。";return false;}
    if(finalUrl){DWORD count=0;WinHttpQueryOption(request,WINHTTP_OPTION_URL,nullptr,&count);if(!count||count>65536){error=L"无法读取最新发布地址。";return false;}std::vector<wchar_t> address(count/sizeof(wchar_t)+1,0);if(!WinHttpQueryOption(request,WINHTTP_OPTION_URL,address.data(),&count)){error=L"无法读取最新发布地址。";return false;}*finalUrl=address.data();return true;}
    data.clear();ULONGLONG deadline=GetTickCount64()+60000;
    for(;;){DWORD available=0;if(GetTickCount64()>deadline||!WinHttpQueryDataAvailable(request,&available)){error=L"下载超时或连接中断。";return false;}if(!available)break;
        if(available>limit-data.size()){error=L"更新文件超过大小限制。";return false;}
        size_t offset=data.size();data.resize(offset+available);DWORD read=0;if(!WinHttpReadData(request,data.data()+offset,available,&read)||!read){error=L"下载连接中断。";return false;}data.resize(offset+read);
    }return true;
}
static bool redirectedRelease(const S& url,const S& current,Release& output){
    const S prefix=L"https://github.com/Phoenix1996/shuweibing/releases/tag/v";
    if(url.compare(0,prefix.size(),prefix)!=0)return false;
    S number=url.substr(prefix.size());std::array<int,3> next{},old{};if(!version(number,next)||!version(current,old))return false;
    Release candidate;if(next>old){candidate.version=number;candidate.exeUrl=Prefix+L"v"+number+L"/ShuWeiBing.exe";candidate.sumUrl=Prefix+L"v"+number+L"/SHA256SUMS.txt";candidate.body=L"GitHub API 暂时受限，已通过项目最新正式发布链接检查版本。完整更新说明请查看 GitHub 发布页面。";}output=std::move(candidate);return true;
}
static bool latest(const S& current,Release& output,S& error){
    std::vector<unsigned char> bytes;DWORD status=0;
    if(get(L"https://api.github.com/repos/Phoenix1996/shuweibing/releases/latest",1024*1024,bytes,error,nullptr,&status)){
        if(parseRelease(bytes,current,output))return true;error=L"更新信息不完整，请稍后重试或访问项目首页。";return false;
    }
    // The official latest-release redirect excludes drafts and prereleases. Use
    // it only for API access/rate limits; validate the final owner, repo and tag.
    if(status!=403&&status!=429)return false;
    S address;if(!get(L"https://github.com/Phoenix1996/shuweibing/releases/latest",0,bytes,error,&address))return false;
    if(!redirectedRelease(address,current,output)){error=L"最新发布地址无效，请访问项目首页。";return false;}error.clear();return true;
}
static S sha256(const std::vector<unsigned char>& bytes){
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;S result;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    DWORD objectSize=0,done=0;BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&objectSize,sizeof(objectSize),&done,0);std::vector<unsigned char> object(objectSize);unsigned char digest[32]{};
    if(BCryptCreateHash(alg,&hash,object.data(),objectSize,nullptr,0,0)>=0&&BCryptHashData(hash,(PUCHAR)bytes.data(),(ULONG)bytes.size(),0)>=0&&BCryptFinishHash(hash,digest,sizeof(digest),0)>=0){const wchar_t* hex=L"0123456789abcdef";for(auto b:digest){result+=hex[b>>4];result+=hex[b&15];}}
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);return result;
}
static S expectedHash(const std::vector<unsigned char>& bytes){S sums=wide((const char*)bytes.data(),bytes.size());size_t at=0;while(at<sums.size()){size_t end=sums.find(L'\n',at);if(end==S::npos)end=sums.size();S line=sums.substr(at,end-at);if(!line.empty()&&line.back()==L'\r')line.pop_back();if(line.size()==80&&line.substr(64)==L"  ShuWeiBing.exe"){S value=line.substr(0,64);if(value.find_first_not_of(L"0123456789abcdefABCDEF")!=S::npos)return {};std::transform(value.begin(),value.end(),value.begin(),towlower);return value;}at=end+1;}return {};}
static bool writeFile(const S& path,const std::vector<unsigned char>& bytes){HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return false;DWORD n=0;bool ok=WriteFile(f,bytes.data(),(DWORD)bytes.size(),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(f);CloseHandle(f);return ok;}
static bool readFile(const S& path,std::vector<unsigned char>& data){HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return false;LARGE_INTEGER size{};bool ok=GetFileSizeEx(f,&size)&&size.QuadPart>0&&size.QuadPart<=20*1024*1024;if(ok){data.resize((size_t)size.QuadPart);DWORD n=0;ok=ReadFile(f,data.data(),(DWORD)data.size(),&n,nullptr)&&n==data.size();}CloseHandle(f);return ok;}
struct Request {HWND owner;bool manual,download;Release release;};
static DWORD WINAPI work(void* context){auto request=(Request*)context;auto result=new Result;result->manual=request->manual;result->download=request->download;
    if(request->download){result->release=request->release;std::vector<unsigned char> sums,exe;
        if(get(request->release.sumUrl,16384,sums,result->error)&&get(request->release.exeUrl,20*1024*1024,exe,result->error)){
            S expected=expectedHash(sums);if(expected.empty()||sha256(exe)!=expected)result->error=L"下载文件校验未通过，已取消更新。";
            else{wchar_t local[32768]{};DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);S base=n&&n<32768?S(local)+L"\\ShuWeiBing":S();if(base.empty())result->error=L"无法找到更新保存目录。";
                else{CreateDirectoryW(base.c_str(),nullptr);S folder=base+L"\\updates";CreateDirectoryW(folder.c_str(),nullptr);folder+=L"\\"+request->release.version;CreateDirectoryW(folder.c_str(),nullptr);S file=folder+L"\\ShuWeiBing.exe";
                    if(!writeFile(file,exe))result->error=L"无法保存更新文件。";else if(fileVersion(file)!=request->release.version){DeleteFileW(file.c_str());result->error=L"下载程序的版本不匹配。";}else result->file=file;
                }
            }
        }
    }else{latest(currentVersion(),result->release,result->error);}
    DWORD pid=0;GetWindowThreadProcessId(request->owner,&pid);if(pid!=GetCurrentProcessId()||!PostMessageW(request->owner,Message,0,(LPARAM)result))delete result;delete request;return 0;
}
static bool start(Request* request){HANDLE t=CreateThread(nullptr,0,work,request,0,nullptr);if(!t){delete request;return false;}CloseHandle(t);return true;}
bool check(HWND owner,bool manual){return start(new Request{owner,manual,false,{}});}
bool download(HWND owner,const Release& release){return start(new Request{owner,true,true,release});}
static bool replaceFile(const S& source,const S& target){
    DWORD attributes=GetFileAttributesW(target.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return false;
    if(!CopyFileW(target.c_str(),(target+L".backup").c_str(),FALSE))return false;
    S temp=target+L".new";if(!CopyFileW(source.c_str(),temp.c_str(),FALSE))return false;
    if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp.c_str());return false;}return true;
}
static void reopen(const S& path){S cmd=quote(path)+L" --skip-update-check";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};if(CreateProcessW(path.c_str(),cmd.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)){CloseHandle(process.hThread);CloseHandle(process.hProcess);}}
static bool helper(const S& file,const S& target,DWORD pid,bool noRestart,S& error,HANDLE* kept=nullptr){
    S eventName=L"Local\\ShuWeiBing.Update."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());if(!ready){error=L"无法建立更新交接。";return false;}
    S cmd=quote(file)+L" --apply-update "+std::to_wstring(pid)+L" "+quote(target)+L" "+quote(eventName)+(noRestart?L" --no-restart":L"");STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    bool created=CreateProcessW(file.c_str(),cmd.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)!=0,ok=false;
    if(created){HANDLE waits[]={ready,process.hProcess};ok=WaitForMultipleObjects(2,waits,FALSE,5000)==WAIT_OBJECT_0;CloseHandle(process.hThread);if(ok&&kept)*kept=process.hProcess;else CloseHandle(process.hProcess);}
    CloseHandle(ready);if(!ok)error=L"无法启动更新交接，请从 GitHub 下载新版后手动替换。";return ok;
}
bool handoff(const S& file,S& error){return helper(file,ownPath(),GetCurrentProcessId(),false,error);}
bool probe(const S& output){
    std::vector<unsigned char> metadata,sums,exe;S error;Release release;
    bool ok=latest(L"0.0.0",release,error)&&!release.version.empty();S stage=L"metadata";
    if(ok){ok=get(release.sumUrl,16384,sums,error);stage=L"checksums";}
    if(ok){ok=get(release.exeUrl,20*1024*1024,exe,error);stage=L"executable";}
    S expected=expectedHash(sums);ok=ok&&!expected.empty()&&sha256(exe)==expected;
    S report=ok?L"PASS GitHub latest release and executable SHA256 verified\n":L"FAIL stage="+stage+L" error="+error+L"\n";
    int count=WideCharToMultiByte(CP_UTF8,0,report.c_str(),(int)report.size(),nullptr,0,nullptr,nullptr);std::vector<unsigned char> bytes(count);WideCharToMultiByte(CP_UTF8,0,report.c_str(),(int)report.size(),(char*)bytes.data(),count,nullptr,nullptr);writeFile(output,bytes);return ok;
}
int applyMode(int argc,wchar_t** argv){
    if(argc<2||S(argv[1])!=L"--apply-update")return -1;
    if(argc<5)return 1;wchar_t* end=nullptr;unsigned long pid=wcstoul(argv[2],&end,10);if(!pid||!end||*end||pid==GetCurrentProcessId())return 1;
    S target=fullPath(argv[3]),self=ownPath();HANDLE parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!parent)return 1;
    wchar_t path[32768]{};DWORD size=32768;bool valid=QueryFullProcessImageNameW(parent,0,path,&size)&&samePath(target,fullPath(S(path,size)))&&!samePath(target,fullPath(self));
    HANDLE ready=valid?OpenEventW(EVENT_MODIFY_STATE,FALSE,argv[4]):nullptr;if(!ready){CloseHandle(parent);return 1;}
    bool noRestart=argc>5&&S(argv[5])==L"--no-restart";SetEvent(ready);CloseHandle(ready);DWORD done=WaitForSingleObject(parent,30000);CloseHandle(parent);if(done!=WAIT_OBJECT_0)return 1;
    bool ok=replaceFile(self,target);if(!ok&&!noRestart)MessageBoxW(nullptr,L"无法替换原程序，已保留原文件。请检查安装目录权限，或从 GitHub 下载后手动更新。",L"鼠卫兵 · 更新失败",MB_OK|MB_ICONWARNING);
    if(!noRestart)reopen(target);return ok?0:1;
}
void selfTest(const S& dir,const std::function<void(bool,const char*)>& check){
    std::array<int,3> a{},b{};check(version(L"0.3.10",a)&&version(L"0.3.9",b)&&a>b&&!version(L"0.3.3evil",b),"update versions compare numerically and reject suffix injection");
    check(quote(L"C:\\中文 空格\\app.exe")==L"\"C:\\中文 空格\\app.exe\"","update helper quotes Unicode paths with spaces");
    auto json=[](const std::string& s){return std::vector<unsigned char>(s.begin(),s.end());};
    const std::string metadata=R"({"tag_name":"v0.3.4","draft":false,"body":"Update","assets":[{"name":"ShuWeiBing.exe","size":500000,"browser_download_url":"https://github.com/Phoenix1996/shuweibing/releases/download/v0.3.4/ShuWeiBing.exe"},{"name":"SHA256SUMS.txt","browser_download_url":"https://github.com/Phoenix1996/shuweibing/releases/download/v0.3.4/SHA256SUMS.txt"}]})";
    Release r;check(parseRelease(json(metadata),L"0.3.3",r)&&r.version==L"0.3.4"&&!r.exeUrl.empty(),"update parses complete release assets from the configured repository");
    Release none;check(parseRelease(json(metadata),L"0.3.4",none)&&none.version.empty(),"current or older release does not prompt an update");
    auto prerelease=metadata;prerelease.insert(1,"\"prerelease\":true,");check(!parseRelease(json(prerelease),L"0.3.3",r),"updater rejects prerelease metadata");
    check(redirectedRelease(L"https://github.com/Phoenix1996/shuweibing/releases/tag/v0.3.4",L"0.3.3",r)&&r.version==L"0.3.4"&&r.exeUrl==Prefix+L"v0.3.4/ShuWeiBing.exe","rate limit fallback validates official release redirect and constructs pinned asset URLs");
    check(!redirectedRelease(L"https://github.com/other/shuweibing/releases/tag/v0.3.4",L"0.3.3",r)&&!redirectedRelease(L"https://github.com/Phoenix1996/shuweibing/releases/tag/v0.3.4?other",L"0.3.3",r)&&!redirectedRelease(L"http://github.com/Phoenix1996/shuweibing/releases/tag/v0.3.4",L"0.3.3",r),"rate limit fallback rejects foreign repository, non-HTTPS and modified tags");
    check(redirectedRelease(L"https://github.com/Phoenix1996/shuweibing/releases/tag/v0.3.2",L"0.3.3",none)&&none.version.empty(),"rate limit fallback never offers a downgrade");
    auto altered=metadata;auto at=altered.find("Phoenix1996");altered.replace(at,11,"otherowner");check(!parseRelease(json(altered),L"0.3.3",r)&&!parseRelease(json("{bad"),L"0.3.3",r),"update rejects foreign download URL and malformed JSON");
    const S hash=L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    check(sha256(json("abc"))==hash&&sha256(json("abd"))!=hash,"native SHA256 detects changed update payload");
    check(expectedHash(json("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  ShuWeiBing.exe\n"))==hash&&expectedHash(json("bad  ShuWeiBing.exe" )).empty(),"update checksum selects exact executable and validates digest");
    S fixture=dir+L"\\更新 测试";CreateDirectoryW(fixture.c_str(),nullptr);S old=fixture+L"\\old.exe",source=fixture+L"\\new.exe";writeFile(old,json("old"));writeFile(source,json("new"));std::vector<unsigned char> bytes,backup;
    bool replaced=replaceFile(source,old)&&readFile(old,bytes)&&readFile(old+L".backup",backup);check(replaced&&bytes==json("new")&&backup==json("old"),"update replacement preserves prior binary as a backup");
    check(!replaceFile(fixture+L"\\missing",old)&&readFile(old,bytes)&&bytes==json("new"),"failed update copy leaves the current binary intact");
    // End-to-end helper waits on and replaces only a dedicated synthetic parent.
    S probe=ownPath();probe=probe.substr(0,probe.find_last_of(L"\\"))+L"\\..\\..\\.build\\dpi-probe.exe";S parentFile=fixture+L"\\parent.exe",helperFile=fixture+L"\\helper.exe",record=fixture+L"\\parent.txt";
    CopyFileW(probe.c_str(),parentFile.c_str(),FALSE);CopyFileW(ownPath().c_str(),helperFile.c_str(),FALSE);DeleteFileW(record.c_str());S command=quote(parentFile)+L" --hold "+quote(record);STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION parent{};HANDLE applying=nullptr;S error;bool integrated=false;
    if(CreateProcessW(parentFile.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&parent)){
        CloseHandle(parent.hThread);ULONGLONG until=GetTickCount64()+4000;while(GetTickCount64()<until&&!readFile(record,bytes))Sleep(20);
        if(bytes.size()>=4){S contents((const wchar_t*)bytes.data(),bytes.size()/2);HWND w=(HWND)(uintptr_t)_wcstoui64(contents.c_str()+1,nullptr,10);
            if(helper(helperFile,parentFile,parent.dwProcessId,true,error,&applying)){PostMessageW(w,WM_CLOSE,0,0);if(WaitForSingleObject(applying,5000)==WAIT_OBJECT_0){DWORD code=1;GetExitCodeProcess(applying,&code);std::vector<unsigned char> actual,expected;integrated=code==0&&readFile(parentFile,actual)&&readFile(helperFile,expected)&&sha256(actual)==sha256(expected);}CloseHandle(applying);}
        }
        if(WaitForSingleObject(parent.hProcess,2000)!=WAIT_OBJECT_0)TerminateProcess(parent.hProcess,1);CloseHandle(parent.hProcess);
    }
    check(integrated,"update helper handoff replaces only its validated synthetic parent after exit");
}
}

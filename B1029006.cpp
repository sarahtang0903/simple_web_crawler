#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include "WinSock2.h"
#include <time.h>
#include <queue>
#include <direct.h>
#include <unordered_set>
#include <regex>
#include <unordered_map>
#include <sys/stat.h>
#include <time.h>
#include <chrono>

using namespace std;
using namespace std::chrono;

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_PAGE_BUF_SIZE 1048576
queue<string> hrefUrl;
unordered_set<string> visitedUrl;
unordered_set<string> visitedImg;
unordered_map<string, string> originalImgUrls;

bool IsURLValid(const string &url)
{
    // 定義 URL 的正則表達式
    regex urlRegex("^https?://[\\w\\.-]+(:\\d+)?(/\\S*)?$");

    // 使用 regex_match 函數檢查是否匹配
    return regex_match(url, urlRegex);
}

// 解析url
bool ParseURL(const string &url, string &host, string &resource)
{
    if (strlen(url.c_str()) > 2000)
    {
        return false;
    }

    const char *pos = strstr(url.c_str(), "http://");
    if (pos == NULL)
        pos = url.c_str();
    else
        pos += strlen("http://");
    if (strstr(pos, "/") == NULL)
        return false;
    char pHost[100];
    char pResource[2000];
    sscanf(pos, "%[^/]%s", pHost, pResource);
    host = pHost;
    resource = pResource;
    return true;
}

// 使用 Get
bool GetHttpResponse(const string &url, char *&response, int &bytesRead)
{
    string fullUrl;
    string host, resource;
    if (url.find("http://") == string::npos)
    {
        // 如果是相對 URL
        fullUrl = "http://" + host + "/" + url;
    }
    else
    {
        fullUrl = url;
    }

    // 解析 URL
    if (!ParseURL(fullUrl, host, resource))
    {
        cout << "Can not parse the url" << endl;
        return false;
    }

    // 建立 socket
    struct hostent *hp = gethostbyname(host.c_str());
    if (hp == NULL)
    {
        cout << "Can not find host address" << endl;
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1 || sock == -2)
    {
        cout << "Can not create sock." << endl;
        return false;
    }

    // 設置 Socket 連接的地址
    SOCKADDR_IN sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(80);
    memcpy(&sa.sin_addr, hp->h_addr, 4);

    // 通過 connect 函數與主機建立連接
    if (0 != connect(sock, (SOCKADDR *)&sa, sizeof(sa)))
    {
        cout << "Can not connect: " << fullUrl << endl;
        closesocket(sock);
        return false;
    };

    // 構建 HTTP GET 請求並通過 send 函數發送 HTTP GET請求
    string request = "GET " + resource + " HTTP/1.1\r\nHost:" + host + "\r\nConnection:Close\r\n\r\n";

    if (SOCKET_ERROR == send(sock, request.c_str(), request.size(), 0))
    {
        cout << "send error" << endl;
        closesocket(sock);
        return false;
    }

    // 接收 HTTP response
    int m_nContentLength = DEFAULT_PAGE_BUF_SIZE;
    char *pageBuf = (char *)malloc(m_nContentLength);
    memset(pageBuf, 0, m_nContentLength);

    bytesRead = 0;
    int ret = 1;
    while (ret > 0)
    {
        ret = recv(sock, pageBuf + bytesRead, m_nContentLength - bytesRead, 0);

        if (ret > 0)
        {
            bytesRead += ret;
        }

        if (m_nContentLength - bytesRead < 100)
        {
            cout << "\nRealloc memorry" << endl;
            m_nContentLength *= 2;
            pageBuf = (char *)realloc(pageBuf, m_nContentLength); // 重新分配內存
        }
    }
    cout << endl;

    pageBuf[bytesRead] = '\0';
    response = pageBuf;
    closesocket(sock);
    return true;
}

// 提取其中的圖片 URL
vector<string> HTMLParse(string &htmlResponse, vector<string> &imgurls, vector<string> &pageUrls, const string &host)
{
    vector<string> originurls;
    const char *p = htmlResponse.c_str();
    const char *tag = "href=\"";
    const char *pos = strstr(p, tag);

    tag = "<img ";
    const char *att1 = "src=\"";
    const char *att2 = "lazy-src=\"";
    const char *pos0 = strstr(p, tag);
    while (pos0)
    {
        pos0 += strlen(tag);
        const char *pos2 = strstr(pos0, att2);
        if (!pos2 || pos2 > strstr(pos0, ">"))
        {
            pos = strstr(pos0, att1);
            if (!pos)
            {
                pos0 = strstr(att1, tag);
                continue;
            }
            else
            {
                pos = pos + strlen(att1);
            }
        }
        else
        {
            pos = pos2 + strlen(att2);
        }

        const char *nextQ = strstr(pos, "\"");
        if (nextQ)
        {
            char *url = new char[nextQ - pos + 1];
            sscanf(pos, "%[^\"]", url);
            originurls.push_back(url);
            string imgUrl = url;

            // Check if lazy-src exists, if yes, use it as the image URL
            const char *lazySrc = strstr(pos0, att2);
            if (lazySrc && lazySrc < nextQ)
            {
                sscanf(lazySrc + strlen(att2), "%[^\"]", url);
                imgUrl = url;
            }

            // Construct full URL if imgUrl is a relative path
            if (imgUrl.find("http") != 0)
            {
                // Check if imgUrl starts with "/"
                if (imgUrl.find("/") == 0)
                {
                    imgUrl = "http://" + host + imgUrl;
                }
                else
                {
                    // If it doesn't start with "/", it's a relative path; append it to the current page URL
                    imgUrl = "http://" + host + "/" + imgUrl;
                }
            }
            if (visitedImg.find(imgUrl) == visitedImg.end())
            {
                visitedImg.insert(imgUrl);
                imgurls.push_back(imgUrl);
            }
            pos0 = strstr(pos0, tag);
            delete[] url;
        }
    }
    // cout << "end of Parse this html" << endl;
    return originurls;
}

string ToIMGFileName(const string &url)
{
    string fileName;
    fileName.resize(url.size());
    int k = 0;
    for (int i = 0; i < (int)url.size(); i++)
    {
        char ch = url[i];
        if (ch != '\\' && ch != '/' && ch != ':' && ch != '*' && ch != '?' && ch != '"' && ch != '<' && ch != '>' && ch != '|' && ch != '.')
            fileName[k++] = ch;
    }
    return fileName.substr(0, k) + ".jpg";
}

string ToFileName(const string &url)
{
    string fileName;
    fileName.resize(url.size());
    int k = 0;
    for (int i = 0; i < (int)url.size(); i++)
    {
        char ch = url[i];
        if (ch != '\\' && ch != '/' && ch != ':' && ch != '*' && ch != '?' && ch != '"' && ch != '<' && ch != '>' && ch != '|' && ch != '.')
            fileName[k++] = ch;
    }
    return fileName.substr(0, k) + ".html";
}

void ModifyHTMLImagesWithLocalPaths(const string &filePath, const unordered_map<string, string> &originalImgUrls)
{

    ifstream inputFile(filePath);
    if (!inputFile.is_open())
    {
        cerr << "Failed to open input file: " << filePath << endl;
        return;
    }

    // 讀取整個文件內容
    string fileContent((istreambuf_iterator<char>(inputFile)), istreambuf_iterator<char>());
    inputFile.close();

    for (const auto &pair : originalImgUrls)
    {

        const string &originurls = pair.first;
        const string &localPath = pair.second;

        // debug
        //  cout << "originurls: " << pair.first << ", localpath: " << pair.second << endl;

        string pattern = "(src\\s*=\\s*\")(" + originurls + ")(\")";
        std::regex regexPattern(pattern);
        std::smatch match;

        if (std::regex_search(fileContent, match, regexPattern))
        {
            std::string srcMatch = match[0].str();
            std::string replacedSrc = match[1].str() + "." + localPath + match[3].str();
            fileContent.replace(match.position(), srcMatch.length(), replacedSrc);
        }
    }

    // 寫回文件
    ofstream outputFile(filePath);
    if (!outputFile.is_open())
    {
        cerr << "Failed to open output file: " << filePath << endl;
        return;
    }

    outputFile << fileContent;
    outputFile.close();

    cout << "HTML file modified successfully." << endl;
}

// 下載圖片
//

bool DownloadImage(const string &imageUrl, const string &outputFileName, unordered_map<string, string> &originalImgUrls, const string &filename)
{
    auto startTime = steady_clock::now();
    char *response;
    int bytes;

    // 用 GET 請求獲取圖片
    if (!GetHttpResponse(imageUrl, response, bytes))
    {
        cout << "Failed to get HTTP response for image: " << imageUrl << endl;
        return false;
    }

    // 檢查 Content-Type
    const string contentTypeHeader = "Content-Type:";
    const char *contentTypePos = strstr(response, contentTypeHeader.c_str());
    if (contentTypePos != nullptr)
    {
        contentTypePos += contentTypeHeader.length();
        const char *endOfLine = strstr(contentTypePos, "\r\n");
        if (endOfLine != nullptr)
        {
            string contentType(contentTypePos, endOfLine - contentTypePos);

            // 檢查 Content-Type 是否為圖片格式
            if (contentType.find("image/") != string::npos)
            {
                // 寫入圖片文件
                ofstream imageFile(outputFileName, ios::binary);
                if (imageFile.is_open())
                {
                    // 找到圖片數據的開始位置
                    const char *imageDataStart = strstr(response, "\r\n\r\n");
                    if (imageDataStart != nullptr)
                    {
                        imageDataStart += strlen("\r\n\r\n");
                        int imageBytes = bytes - (imageDataStart - response);
                        imageFile.write(imageDataStart, imageBytes);
                        imageFile.close();
                        cout << "Downloaded image: " << outputFileName << endl;
                        // originalImgUrls[imageUrl] = outputFileName;
                        free(response);
                        auto endTime = steady_clock::now();
                        auto elapsedTime = duration_cast<milliseconds>(endTime - startTime).count() / 1000.0; // 換算成秒

                        // 計算速度
                        double speed = bytes / elapsedTime / 1024.0; // 每秒的 KB 數

                        // 顯示速度和預計剩餘完成時間
                        cout << "Download speed: " << speed << " KB/s" << endl;
                        cout << "Download size: " << imageBytes << "  bytes" << endl;

                        return true;
                    }
                    else
                    {
                        cout << "Failed to find image data in HTTP response." << endl;
                    }
                }
                else
                {
                    cout << "Failed to create image file: " << outputFileName << endl;
                }
            }
        }

        cout << "Invalid or missing Content-Type header for image: " << imageUrl << endl;
        free(response);
        return false;
    }
}

int main(int argc, char *argv[])
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        return 1;
    }
    string urlStart;
    string dirfile;
    string userOutputDir;
    if (argc >= 3)
    {
        string userURL = argv[1];
        string outputDir = argv[2];
        if (IsURLValid(userURL))
        {
            cout << "URL is valid." << endl;
        }
        else
        {
            cout << "Invalid URL format." << endl;
        }
        _mkdir("./img");
        userOutputDir = "./" + outputDir;
        if (_mkdir(userOutputDir.c_str()) == 0)
        {
            cout << "Output directory created: " << userOutputDir << endl;
        }
        else
        {
            cout << "Failed to create output directory." << endl;
        }
        urlStart = userURL;
        dirfile = userOutputDir;
    }
    else if (argc == 1)
    {
        string userURL, outputDir;

        cout << "Enter the URL: ";
        getline(cin, userURL);

        cout << "Enter the output directory: ";
        getline(cin, outputDir);

        if (IsURLValid(userURL))
        {
            cout << "URL is valid." << endl;
        }
        else
        {
            cout << "Invalid URL format." << endl;
        }

        _mkdir("./img");
        userOutputDir = "./" + outputDir;
        if (_mkdir(userOutputDir.c_str()) == 0)
        {
            cout << "Output directory created: " << userOutputDir << endl;
        }
        else
        {
            cout << "Failed to create output directory." << endl;
        }
        urlStart = userURL;
        dirfile = userOutputDir;
    }
    else
    {
        cout << "Usage:" << endl;
        cout << "Command Line Mode: program_name URL output_directory" << endl;
        cout << "Interactive Mode: program_name" << endl;
    }
    string filename;

    char *response;
    int bytes;
    int totalDownloadedFiles = 0;
    int totalDownloadSize = 0;
    int totalDownloadedBytes = 0;
    string outputdir;
    if (GetHttpResponse(urlStart, response, bytes))
    {
        string httpResponse = response;
        const char *htmlContentStart = strstr(response, "\r\n\r\n");
        if (htmlContentStart != nullptr)
        {
            htmlContentStart += strlen("\r\n\r\n");

            // Save only the HTML content to the file
            string htmlContent = htmlContentStart;

            filename = ToFileName(urlStart);
            outputdir = dirfile.c_str();
            // cout << "outputdir" << outputdir << endl;
            ofstream ofile(outputdir + "/" + filename);
            if (ofile.is_open())
            {
                ofile << htmlContent << endl;
                ofile.close();
            }
        }

        string host;
        string resource;
        vector<string> originurls;
        if (ParseURL(urlStart, host, resource))
        {
            vector<string> imgurls;
            vector<string> pageUrls;

            originurls = HTMLParse(httpResponse, imgurls, pageUrls, host);
            // debug
            //  for (int i = 0; i < originurls.size(); ++i)
            //  {
            //      cout << "originurls:" << originurls[i] << endl;
            //  }
            clock_t start = clock();
            for (int i = 0; i < imgurls.size(); i++)
            {
                string imgFileName = ToIMGFileName(imgurls[i]);
                string imgUrl = imgurls[i];
                string localPath = "./img/" + imgFileName;
                // debug
                //  cout << "localpath_inside:" << localPath << endl;
                string reoriginurl = originurls[i];
                originalImgUrls[reoriginurl] = localPath;
                if (DownloadImage(imgUrl, "./img/" + imgFileName, originalImgUrls, filename))
                {
                    totalDownloadedFiles++;
                    cout << "Downloading progress: " << (i + 1) << " / " << imgurls.size() << endl
                         << endl;
                    // totalDownloadedBytes += imageBytes;
                }
            }
            clock_t end = clock();
            double elapsedTime = double(end - start) / CLOCKS_PER_SEC;
            cout << "Total execution time: " << elapsedTime << " seconds" << endl
                 << endl;
            cout << "Total downloaded files: " << totalDownloadedFiles << endl;
        }
        else
        {
            cout << "Failed to parse URL: " << urlStart << endl;
        }
        ModifyHTMLImagesWithLocalPaths(outputdir + "/" + filename, originalImgUrls);
    }
    else
    {
        cout << "Failed to get HTTP response." << endl;
    }

    WSACleanup();
    return 0;
}

#include "commroute.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

void BinToHex(char* Buffer, char* Text, int BufSize)
{
  static char Convert[16] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};

  for (int I = 0; I < BufSize; I++)
  {
    Text[0] = Convert[(unsigned char)(Buffer[I]) >> 4];
    Text[1] = Convert[(unsigned char)(Buffer[I]) & 0x0F];
	Text = Text + 2;
  }
}

int HexToBin(char* Text, char* Buffer, int BufSize)
{
	static char Convert[55] =
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,-1,
	10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,10,11,12,13,14,15 };

	int i =  BufSize;
	while (i > 0)
	{
		if (Text[0] < '0' || Text[1] < '0'  || Text[0] > 'f' || Text[1] > 'f'
			|| Convert[Text[0]-'0'] == -1 || Convert[Text[1]-'0'] == -1)
			break;
		Buffer[0] = char((Convert[Text[0]-'0'] << 4) + Convert[Text[1]-'0']);
		Buffer++;
		Text += 2;
		i--;
	}
	return BufSize - i;
}

unsigned int BCDToDec(const char *bcd, int length)
{
	unsigned int dec = 0;
	for(int i=0; i<length; i++)
	{
		unsigned int tmp = ((bcd[i]>>4)&0x0F)*10 + (bcd[i]&0x0F);
		dec = dec * 100 + tmp;
	}
	return dec;
}


string Str_TrimA(const string& str)
{
    string::size_type pos = str.find_first_not_of(' ');
    if (pos == string::npos)
    {
        return str;
    }
    string::size_type pos2 = str.find_last_not_of(' ');
    if (pos2 != string::npos)
    {
        return str.substr(pos, pos2 - pos + 1);
    }
    return str.substr(pos);
}


string GetSysDateTimeStr(void) {
	char buffer[50] = {0};
	time_t now; 
	struct tm dt; 
	time(&now); //time函数读取现在的时间(国际标准时间非北京时间)，然后传值给now
	//struct tm* dt; //实例化tm结构指针
	//dt = localtime(&now);
	localtime_r(&now, &dt);  //线程安全版本
	string strFormat("[%04d-%02d-%02d %02d:%02d:%02d] ");
	snprintf(buffer, sizeof(buffer), strFormat.data(), dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour,
		dt.tm_min, dt.tm_sec);
	return string(buffer);
	
}

string GetDateStr()
{
	char buffer[50] = { 0 };
	time_t now;
	struct tm dt;
	time(&now); //time函数读取现在的时间(国际标准时间非北京时间)，然后传值给now
	//struct tm* dt; //实例化tm结构指针
	//dt = localtime(&now);
	localtime_r(&now, &dt);  //线程安全版本
	string strFormat("%04d%02d%02d");
	snprintf(buffer, sizeof(buffer), strFormat.data(), dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour,
		dt.tm_min, dt.tm_sec);
	return string(buffer);
}

bool FileExists(const char* FileName)
{
	return (access(FileName, F_OK) != -1);
}

unsigned long long GetTickCount64()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}


char* GetExePath(char* buf, int ibufSize)
{
	ssize_t rslt = readlink("/proc/self/exe", buf, ibufSize - 1);
	if (rslt < 0 || (rslt >= ibufSize - 1))
	{
		return NULL;
	}
	buf[rslt] = '\0';
	for (ssize_t i = rslt; i >= 0; i--)
	{
		if (buf[i] == '/')
		{
			buf[i + 1] = '\0';
			break;
		}
	}
	return buf;
}

//实例是否已存在
bool IsInstanceExists(const char* instName)
{
	int  fd;
	char buf[16];
	char dir[256];
	char filename[256];
	GetExePath(dir, sizeof(dir));
	snprintf(filename, sizeof(filename), "%s/.%s.pid", dir, instName);
	fd = open(filename, O_RDWR | O_CREAT, (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
	if (fd < 0) {
		printf("open file %s failed!\n", filename);
		return true;
	}

	struct flock fl;
	fl.l_type = F_WRLCK;
	fl.l_start = 0;
	fl.l_whence = SEEK_SET;
	fl.l_len = 0;
	// 对文件进行加锁,失败表明文件已经被其他进程锁了 
	if (fcntl(fd, F_SETLK, &fl) == -1) {   
		//printf("file %s locked. proc already exit!\n", filename);
		close(fd);
		return true;
	}
	else { //没有锁，就写入运行实例的pid 
		ftruncate(fd, 0);  
		snprintf(buf, sizeof(buf), "%ld", (long)getpid());
		write(fd, buf, strlen(buf) + 1);
		return false;
	}
}

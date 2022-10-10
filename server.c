#include "ondi.h"

#define  DATABASE  "my.db"

int do_client(int acceptfd, sqlite3 *db);
void do_register(int acceptfd, MSG *msg, sqlite3 *db);
int do_login(int acceptfd, MSG *msg, sqlite3 *db);
int do_query(int acceptfd, MSG *msg, sqlite3 *db);
int do_history(int acceptfd, MSG *msg, sqlite3 *db);
int history_callback(void* arg,int f_num,char** f_value,char** f_name);
int do_searchword(int acceptfd, MSG *msg, char word[]);
int get_date(char *date);

int main(int argc, const char *argv[])
{

	int sockfd;
	struct sockaddr_in  serveraddr;
	int n;
	MSG  msg;
	sqlite3 *db;
	int acceptfd;
	pid_t pid;

	//入参检查
	if(argc != 3)
	{
		printf("Usage:%s <serverip>  <port>.\n", argv[0]);
		return -1;
	}

	//打开数据库
	if(sqlite3_open(DATABASE, &db) != SQLITE_OK)
	{
		printf("%s\n", sqlite3_errmsg(db));
		return -1;
	}
	else
	{
		printf("open DATABASE success.\n");
	}

	//创建流式套接字
	if((sockfd = socket(AF_INET, SOCK_STREAM,0)) < 0)
	{
		perror("fail to socket.\n");
		return -1;
	}

	bzero(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
	serveraddr.sin_port = htons(atoi(argv[2]));

	//建立套接字与地址联系
	if(bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
	{
		perror("fail to bind.\n");
		return -1;
	}

	//将套接字设为监听模式
	if(listen(sockfd, 5) < 0)
	{
		perror("listen");
		return -1;
	}

	//处理僵尸进程
	signal(SIGCHLD, SIG_IGN);

	while(1)
	{
		if((acceptfd = accept(sockfd, NULL, NULL)) < 0)
		{
			perror("fail to accept");
			return -1;
		}

		if((pid = fork()) < 0)
		{
			perror("fail to fork");
			return -1;
		}
		else if(pid == 0)  // 儿子进程
		{
			//处理客户端具体的消息
			close(sockfd);
			do_client(acceptfd, db);
		}
		else  // 父亲进程,用来接受客户端的请求的
		{
			close(acceptfd);
		}
	}
	
	return 0;
}


int do_client(int acceptfd, sqlite3 *db)
{
	MSG msg;
	//接收消息类型
	while(recv(acceptfd, &msg, sizeof(msg), 0) > 0)
	{
	   switch(msg.type)
	   {
	  	 case R:
			 do_register(acceptfd, &msg, db);
			 break;
		 case L:
			 do_login(acceptfd, &msg, db);
			 break;
		 case Q:
			 do_query(acceptfd, &msg, db);
			 break;
		 case H:
			 do_history(acceptfd, &msg, db);
			 break;
		 default:
			 printf("Invalid data msg.\n");
	   }
	}

	printf("客户端退出\n");
	close(acceptfd);
	exit(0);

	return 0;
}

void do_register(int acceptfd, MSG *msg, sqlite3 *db)
{

	char *errmsg;
	char sql[128];

	sprintf(sql,"insert into usr values('%s','%s');", msg->name, msg->data);

	if(sqlite3_exec(db, sql, NULL, NULL, &errmsg) != SQLITE_OK)
	{
		printf("%s\n", errmsg);
		strcpy(msg->data, "用户名已存在");
	}else{
		printf("客户端注册成功\n");
		strcpy(msg->data, "注册成功!");
	}

	//发送反馈给客户端
	if(send(acceptfd, msg, sizeof(MSG), 0) < 0)
	{
		perror("send");
		return;
	}
	return ;
}

int do_login(int acceptfd, MSG *msg , sqlite3 *db)
{
	char sql[128] = {0};
	char *errmsg;
	int nrow;
	int ncloumn;
	char **resultp;

	sprintf(sql, "select * from usr where name = '%s' and pass = '%s';", msg->name, msg->data);

	if(sqlite3_get_table(db, sql, &resultp, &nrow, &ncloumn, &errmsg) != SQLITE_OK){
		printf("%s\n", errmsg);
		return -1;
	}

	if(nrow == 1){ 		//查询成功
		strcpy(msg->data, "OK");
		send(acceptfd, msg, sizeof(MSG), 0);
		return 1;
	}
	if(nrow == 0){       //数据库中没有
		strcpy(msg->data, "用户名或密码错误");
		send(acceptfd, msg, sizeof(MSG), 0);
	}
	return 0;
}


int do_searchword(int acceptfd, MSG *msg, char word[])
{
	FILE *fp;
	char temp[512] = {0};
	int result;
	int len;
	char *p;

	/*打开文件*/
	fp = fopen("dict.txt", "r");
	if(fp == NULL){
		perror("fopen");
		strcpy(msg->data, "文件打开失败");
		if(send(acceptfd, msg, sizeof(MSG), 0) < 0){
			perror("send");
			return -1;
		}
		return -1;
	}

	len = strlen(word);
	
	//读文件，查询单词
	while(fgets(temp, 512, fp) != NULL)  //一次读一行
	{
		result = strncmp(temp, word, len);

		//FIXME
		/*例如：查询单词word = abc, 找到了tomp = abb...，则继续向下查找*/
		if(result < 0){
			continue;
		}
		/*例如：查询单词word = abc,找到了temp = abd...*/
		/*例如：查询单词word = abc,找到了temp = abcd..*/
		/*表示没找到，直接退出*/
		if(result > 0 || ((result == 0)&&(temp[len] != ' '))) 
		{
			break;
		}
		
		/*表示找到了单词*/
		/*跳到单词的末尾空格处　　abc    v.adsdfs*/
		p = temp + len;
		/*跳跃所有空格*/
		while(*p == ' '){
			p++;
		}

		/*将注释信息拷贝到msg->data*/
		strcpy(msg->data, p);

		/*最后关闭文件文件流指针，返回１表示查找成功*/
		fclose(fp);
		return 1;
	}

	fclose(fp);
	return 0;
}

/*得到系统时间*/
int get_date(char *date)
{
	time_t t;
	struct tm *tp;

	time(&t);

	//进行时间格式转换
	tp = localtime(&t);
	
	/*将时间装进date*/
	sprintf(date, "%4d-%2d-%2d %2d:%2d:%2d", tp->tm_year + 1900, tp->tm_mon+1, tp->tm_mday, 
			tp->tm_hour, tp->tm_min , tp->tm_sec);

	return 0;
}

int do_query(int acceptfd, MSG *msg , sqlite3 *db)
{
	char word[64];
	int found = 0;
	char date[128] = {0};
	char sql[128] = {0};
	char *errmsg;

	/*拿出msg结构体中的单词*/
	strcpy(word, msg->data);

	/*在dict.txt中查询单词word*/
	found = do_searchword(acceptfd,msg, word);

	/*表示找到了单词，将用户名，时间，单词，输入到历史记录表*/
	if(found  == 1){
		get_date(date);
		
		/*将用户名，时间，单词，输入到历史记录表*/
		sprintf(sql, "insert into record values('%s','%s','%s');", msg->name, date, word);

		if(sqlite3_exec(db, sql, NULL, NULL, &errmsg) != SQLITE_OK){
			printf("%s\n", errmsg);
			return -1;
		}
	}
	/*表示没找到*/
	if(found == 0){
		strcpy(msg->data, "没有找到！");
	}

	/*将查询到的结果发送给客户端*/
	if(send(acceptfd, msg, sizeof(MSG), 0) < 0){
		perror("send");
		return -1;
	}
	return 0;
}


// 得到查询结果，并且需要将历史记录发送给客户端
int history_callback(void* arg,int f_num,char** f_value,char** f_name)
{
	//record:name、date、word 
	int acceptfd;
	MSG msg;

	/*查询结果*/
	acceptfd = *((int *)arg);

	/*
	 *f_value[1] data 
	 *f_value[2] word
	 */
	sprintf(msg.data, "%s  %s", f_value[1], f_value[2]);

	send(acceptfd, &msg, sizeof(MSG), 0);

	return 0;
}

int do_history(int acceptfd, MSG *msg, sqlite3 *db)
{
	char sql[128];
	char *errmsg;

	sprintf(sql, "select * from record where name = '%s';", msg->name);
	
	/*回调函数查询历史记录*/
	if(sqlite3_exec(db, sql, history_callback,(void *)&acceptfd, &errmsg) != SQLITE_OK){
		printf("%s\n", errmsg);
	}

	/*所有的历史记录查询发送完，给客户端发送一个结束信息*/
	msg->data[0] = '\0';
	send(acceptfd, msg, sizeof(MSG), 0);

	return 0;
}



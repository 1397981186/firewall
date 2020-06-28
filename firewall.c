#include <gtk/gtk.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <pthread.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <time.h> 
#include <mysql/mysql.h>
#include <unistd.h>

#define REMOTE_SERVER_PORT 80			
#define BUF_SIZE 4096*4 				
#define QUEUE_SIZE 100

pthread_mutex_t conp_mutex;
char lastservername[256] = "";
int lastserverip = 0;
const gchar *text="NULL";			//代理端口输入内容
char inqOrder[2048];				//查询语句输入内容

GtkWidget *shuru=NULL;				//代理端口输入框
GtkWidget *inqEntry=NULL; 			//查询语句输入框
GtkWidget *window_in=NULL; 			//初始窗口
GtkWidget *window_showdata=NULL; 	//界面窗口


int orderNum = 0;	//标明不同的排序要求
int ipup=0;
int toIpup=0;
int reqHostnameup=0;
int reqWayup=0;
int protocolup=0;
int recvStateup=0;
int timeup=0;
int recvcontentup=0;
int sizeup=0;

GtkListStore* model;
GtkTreeIter iter;

GtkWidget *box;
GtkWidget *inqButton;
GtkWidget *delButton;
GtkWidget *freshButton;
GtkWidget *aboutButton;

struct para   
{   
    char *ip;  
    int socketfd;   
};  

enum{
    COLUMN1,
    COLUMN2,
    COLUMN3,
    COLUMN4,COLUMN5,COLUMN6,COLUMN7,COLUMN8,COLUMN9,N_COLUMN
};

struct para connectserver(char* hostname)
{
	printf("at connectserver ! \n");
	int cnt_stat;
	struct hostent *hostinfo;								// info about server
	struct sockaddr_in server_addr;
	struct para remotepara; 							// holds IP address
	remotepara.socketfd=-1;
	remotepara.ip="ConnetWrong";

	int remotesocket;
	int remoteport = REMOTE_SERVER_PORT;  //80
	char newhostname[256];
	char *tmpptr;

	strcpy(newhostname, hostname); 
	tmpptr = strchr(newhostname,':');
	if (tmpptr != NULL)   //port is included in newremotename
	{
		remoteport = atoi(tmpptr + 1); //skip the char ':'
		*tmpptr = '\0';		
	}
		

	remotesocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (remotesocket < 0) {
		printf("can't create socket! \n");
		return remotepara;
	}
	memset(&server_addr, 0, sizeof(server_addr));

	//

	server_addr.sin_family= AF_INET;

	server_addr.sin_port= htons(remoteport);
	pthread_mutex_lock(&conp_mutex);
	if (strcmp(lastservername, newhostname) != 0)
	{ 	
		hostinfo = gethostbyname(newhostname);						
		if (!hostinfo) {
			
			printf("gethostbyname(%s) failed! \n",newhostname);
			pthread_mutex_unlock(&conp_mutex);
			return remotepara;
		}
		strcpy(lastservername,newhostname);
		lastserverip = *(int *)hostinfo->h_addr;
	}
	server_addr.sin_addr.s_addr = lastserverip;
	pthread_mutex_unlock(&conp_mutex);

	

	if (connect(remotesocket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
		printf("remote connect failed! \n");
		close(remotesocket);
		return remotepara;
	}

	remotepara.socketfd=remotesocket;
	remotepara.ip=(char*)inet_ntoa(server_addr.sin_addr);
	printf("connect server! \n");

 	return remotepara;
}

void dealonereq(void *arg)
{
	printf("------------------------at a thread ! \n");
	struct para recv_para,remotepara;
	recv_para=*(struct para*)arg;
	int bytes,bufCount=0;
	int size=0;
	char buf[BUF_SIZE],hostname[256],reqWay[256],reqProtocol[256],reqDate[256];	
	char recvbuf[BUF_SIZE],recvProtocol[256],recvState[256],recvDate[256],recvcontent[256];
	int remotesocket;
	int accept_sockfd = recv_para.socketfd;
	time_t timep; 
    time (&timep); 
    //printf( "------time is  %s ",ctime(&timep)); 
	char* s=recv_para.ip;
	//printf("--------------------------------read ip in dealonereq : %s \n",s);
	pthread_detach(pthread_self());
	bzero(buf,BUF_SIZE);
	bzero(recvbuf,BUF_SIZE);
	bytes = read(accept_sockfd, buf, BUF_SIZE); 	// read a buffer from socket
	printf("read reqbuf------------------------------------------- :%s\n", buf);
	if (bytes <= 0) {	
		close(accept_sockfd);
		printf("buf no thing \n");
		return; 
	}

	getReqInfo(buf,hostname,bytes,reqWay,reqProtocol);
	
	if (sizeof(hostname) == 0) {
		printf("Invalid host name \n");
		close(accept_sockfd);
		return;
	}

	remotepara = connectserver(hostname);
	remotesocket=remotepara.socketfd;
	//printf(" --------------------remote ip  : %s \n",remotepara.ip);
	if (remotesocket == -1){
		close(accept_sockfd);
		return; 
	}
	
	//进行数据库操作，定义相关变量
	MYSQL sql; 
	MYSQL_RES *res;  
	MYSQL_FIELD *field;  
	MYSQL_ROW row;
	mysql_init(&sql);
	if (mysql_real_connect(&sql, "localhost", "root","mysql","logdata", 0, NULL, 0))	//连接数据库
	{
		printf("connect success for database\n");
	}
	else {printf("connect failed for database\n");}

	send(remotesocket, buf, bytes,MSG_NOSIGNAL);
	char  buffer1[2048];
	memset(buffer1, 0, sizeof(buffer1));
	char localtime[256];
	strcpy(localtime,ctime(&timep));
	//localtime=ctime(&timep);
	localtime[strlen(localtime)-1]='\0';
	sprintf(buffer1, "INSERT INTO proxyLog (fromIp ,toIp ,reqHostname ,reqWay ,protocol ,recvState ,time,recvcontent,size) VALUES ('%s','%s','%s','%s','%s','%s','%s','%s',%d)", s,remotepara.ip,hostname,reqWay,reqProtocol,NULL,localtime,NULL,bytes);
	mysql_query(&sql,buffer1);		//将请求的数据包信息写入数据库
	while(1) {
		int readSizeOnce = 0;
		readSizeOnce = read(remotesocket, recvbuf, BUF_SIZE);				
		if (readSizeOnce <= 0) {
			break;
		}
		printf("read recvbuff \n %s", recvbuf);
		send(accept_sockfd, recvbuf, readSizeOnce,MSG_NOSIGNAL);
		size=size+readSizeOnce;
		if(bufCount==0){
			getRecInfo(recvbuf,readSizeOnce,recvProtocol,recvState,recvDate,recvcontent);
			bufCount=bufCount+1;}
		
	}

	char  buffer2[2048];
	memset(buffer2, 0, sizeof(buffer2));
	sprintf(buffer2, "INSERT INTO proxyLog (fromIp ,toIp ,reqHostname ,reqWay ,protocol ,recvState ,time ,recvcontent ,size) VALUES ('%s','%s','%s','%s','%s','%s','%s','%s',%d)", remotepara.ip,s,hostname,NULL,NULL,recvState,recvDate,recvcontent,size);
	mysql_query(&sql,buffer2);	//将返回的数据包信息写入数据库
	
	mysql_close(&sql);
	close(remotesocket);
	close(accept_sockfd);
	printf(" --------------------finish a req ----------------------------------\n");
}

//fresh1到9实现相应的第n列的排序
void fresh1()
{
    	ipup=(ipup+1)%2;
	if(ipup){
			orderNum=11;
			fresh();
		}
	else{
		orderNum=10;
		fresh();
		}
}

void fresh2()
{
    	toIpup=(toIpup+1)%2;
	if(toIpup){
			orderNum=21;
			fresh();
		}
	else{
		orderNum=20;
		fresh();
		}
}


void fresh3()
{
    	reqHostnameup=(reqHostnameup+1)%2;
	if(reqHostnameup){
			orderNum=31;
			fresh();
		}
	else{
		orderNum=30;
		fresh();
		}
}

void fresh4()
{
    	reqWayup=(reqWayup+1)%2;
	if(reqWayup){
			orderNum=41;
			fresh();
		}
	else{
		orderNum=40;
		fresh();
		}
}

void fresh5()
{
    	protocolup=(protocolup+1)%2;
	if(protocolup){
			orderNum=51;
			fresh();
		}
	else{
		orderNum=50;
		fresh();
		}
}

void fresh6()
{
    	recvStateup=(recvStateup+1)%2;
	if(recvStateup){
			orderNum=61;
			fresh();
		}
	else{
		orderNum=60;
		fresh();
		}
}

void fresh7()
{
    	timeup=(timeup+1)%2;
	if(timeup){
			orderNum=71;
			fresh();
		}
	else{
		orderNum=70;
		fresh();
		}
}

void fresh8()
{
    	recvcontentup=(recvcontentup+1)%2;
	if(recvcontentup){
			orderNum=81;
			fresh();
		}
	else{
		orderNum=80;
		fresh();
		}
}

void fresh9()
{
    	sizeup=(sizeup+1)%2;
	if(sizeup){
			orderNum=91;
			fresh();
		}
	else{
		orderNum=90;
		fresh();
		}
}

//自定义查询数据库
void inquire()
{
	gchar *inqchar = gtk_entry_get_text(GTK_ENTRY(inqEntry));
	sprintf(inqOrder,inqchar);
	printf("req : %s",inqOrder);
	orderNum=100;
	fresh();
}

//清空数据库
void delete()
{
	orderNum=110;
	fresh();
}

//数据库指令报错弹窗
void show_warning()
{
	GtkWidget *dialog;
	dialog = gtk_message_dialog_new(window_showdata,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_WARNING,
			GTK_BUTTONS_OK,
			"数据库指令格式错误！\n示例：select * from proxyLog where fromIp='192.168.33.11'");
	gtk_window_set_title(GTK_WINDOW(dialog), "警告");
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

//相关信息按钮
void show_message()
{
  GtkWidget *dialog;
  dialog = gtk_message_dialog_new(window_showdata,
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "版本：1.0\n\n开发人员：盛海涛、谢宸琪\n发布时间：2019.12.25\n\n相关问题联系方式：\nQQ1397981186", "title");
  gtk_window_set_title(GTK_WINDOW(dialog), "关于");
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

//数据库展示界面主要组件
GtkWidget * newScoll()
{
	MYSQL sql; 
	MYSQL_RES *res;  
	MYSQL_FIELD *field;  
	GtkWidget* view;
	MYSQL_ROW row;
	mysql_init(&sql);
	int result;
    GtkTreeModel* model;
    GtkCellRenderer* renderer;
    GtkTreeViewColumn* column;
	GtkWidget *scrlled_window;
	GtkListStore *list_store;
	
	scrlled_window = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrlled_window),
                                   GTK_POLICY_AUTOMATIC,GTK_POLICY_ALWAYS);
	list_store = gtk_list_store_new(9,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);

	if (mysql_real_connect(&sql, "localhost", "root","mysql","logdata", 0, NULL, 0))
	{
		printf("connect success\n");
	}
	//根据点击的不同，执行相应的排序操作
	else {printf("connect failed\n");}
		switch(orderNum){
		case 0:{mysql_query(&sql,"select * from proxyLog");break;}
		case 11:{mysql_query(&sql,"select * from proxyLog ORDER BY fromIp ASC");break;}
		case 10:{mysql_query(&sql,"select * from proxyLog ORDER BY fromIp DESC");break;}
		case 21:{mysql_query(&sql,"select * from proxyLog ORDER BY toIp ASC");break;}
		case 20:{mysql_query(&sql,"select * from proxyLog ORDER BY toIp DESC");break;}
		case 31:{mysql_query(&sql,"select * from proxyLog ORDER BY reqHostname ASC");break;}
		case 30:{mysql_query(&sql,"select * from proxyLog ORDER BY reqHostname DESC");break;}
		case 41:{mysql_query(&sql,"select * from proxyLog ORDER BY reqWay ASC");break;}
		case 40:{mysql_query(&sql,"select * from proxyLog ORDER BY reqWay DESC");break;}
		case 51:{mysql_query(&sql,"select * from proxyLog ORDER BY protocol ASC");break;}
		case 50:{mysql_query(&sql,"select * from proxyLog ORDER BY protocol DESC");break;}
		case 61:{mysql_query(&sql,"select * from proxyLog ORDER BY recvState ASC");break;}
		case 60:{mysql_query(&sql,"select * from proxyLog ORDER BY recvState DESC");break;}
		case 71:{mysql_query(&sql,"select * from proxyLog ORDER BY time ASC");break;}
		case 70:{mysql_query(&sql,"select * from proxyLog ORDER BY time DESC");break;}
		case 81:{mysql_query(&sql,"select * from proxyLog ORDER BY recvcontent ASC");break;}
		case 80:{mysql_query(&sql,"select * from proxyLog ORDER BY recvcontent DESC");break;}
		case 91:{mysql_query(&sql,"select * from proxyLog ORDER BY size ASC");break;}
		case 90:{mysql_query(&sql,"select * from proxyLog ORDER BY size DESC");break;}
		//查询数据库
		case 100:
		{
			result=0;
			result=mysql_query(&sql,inqOrder);
			if (result==1)
			{
				show_warning();
				mysql_query(&sql,"select * from proxyLog");
				result=0;
			}
			orderNum=0;
			break;
		}
		//清空数据库
		case 110:{mysql_query(&sql,"delete from proxyLog");mysql_query(&sql,"select * from proxyLog");break;}
		}
		res=mysql_store_result(&sql);
		while((row=mysql_fetch_row(res))){
			gtk_list_store_append(list_store,&iter);
        		gtk_list_store_set(list_store,&iter,COLUMN1,row[0],COLUMN2,row[1],COLUMN3,row[2],COLUMN4,row[3],COLUMN5,row[4],COLUMN6,row[5],COLUMN7,row[6],COLUMN8,row[7],COLUMN9,row[8],-1);
		}
		
	    mysql_free_result(res);
	    model = GTK_TREE_MODEL(list_store);	
    /*创建一个模型初始化model的控件*/
    view  = gtk_tree_view_new_with_model(model);
    /*创建一个文本单元绘制器*/
	//第一列：源ip，数据库对应fromIp
	//第二列：目的ip，数据库对应toIp
	//第三列：目的hostname，数据库对应reqHostname
	//第四列：请求方式，数据库对应reqWay
	//第五列：协议，数据库对应protocol
	//第六列：返回包状态码，数据库对应recvState
	//第七列：时间，数据库对应time
	//第八列：内容类型，数据库对应recvcontent
	//第九列：大小，数据库对应size
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("源ip",renderer,"text",COLUMN1,NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);
    gtk_tree_view_column_set_clickable (column,1);
    g_signal_connect (column, "clicked", G_CALLBACK (fresh1), NULL);  


    column = gtk_tree_view_column_new_with_attributes("目的ip",renderer,"text",COLUMN2,NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);
    gtk_tree_view_column_set_clickable (column,1);
    g_signal_connect (column, "clicked", G_CALLBACK (fresh2), NULL);  

    column = gtk_tree_view_column_new_with_attributes("目的hostname",renderer,"text",COLUMN3,NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);
    gtk_tree_view_column_set_clickable (column,1);
    g_signal_connect (column, "clicked", G_CALLBACK (fresh3), NULL);  

    column = gtk_tree_view_column_new_with_attributes("请求方式",renderer,"text",COLUMN4,NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);
    gtk_tree_view_column_set_clickable (column,1);
    g_signal_connect (column, "clicked", G_CALLBACK (fresh4), NULL);  

    column = gtk_tree_view_column_new_with_attributes("协议",renderer,"text",COLUMN5,NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);
    gtk_tree_view_column_set_clickable (column,1);
    g_signal_connect (column, "clicked", G_CALLBACK (fresh5), NULL);  

    column= gtk_tree_view_column_new_with_attributes("返回包状态码",renderer,"text",COLUMN6,NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);
    gtk_tree_view_column_set_clickable (column,1);
    g_signal_connect (column, "clicked", G_CALLBACK (fresh6), NULL);  

    column= gtk_tree_view_column_new_with_attributes("时间",renderer,"text",COLUMN7,NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);
    gtk_tree_view_column_set_clickable (column,1);
    g_signal_connect (column, "clicked", G_CALLBACK (fresh7), NULL);  

    column= gtk_tree_view_column_new_with_attributes("内容类型",renderer,"text",COLUMN8,NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);
    gtk_tree_view_column_set_clickable (column,1);
    g_signal_connect (column, "clicked", G_CALLBACK (fresh8), NULL);  

    column = gtk_tree_view_column_new_with_attributes("大小",renderer,"text",COLUMN9,NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);
    gtk_tree_view_column_set_clickable (column,1);
    g_signal_connect (column, "clicked", G_CALLBACK (fresh9), NULL);  

    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrlled_window),view);

	return scrlled_window;
}

struct windowbox
{
	GtkWidget *box;
	GtkWidget *scrlled_window;
};

//刷新整个界面
void fresh()
{
	GtkWidget *scrlled_window;
	GtkWidget* box1;
    GtkWidget* box2;
	GtkWidget* sep;
	GtkWidget* label1;
	
	//移除旧box
	gtk_container_remove(window_showdata,box);
	
	//box是整个窗口，box1是上方的滚动窗口，box2为下方的按钮以及输入框
	//生成新box，box1，box2
	box = gtk_vbox_new(FALSE,10);
	gtk_container_add(GTK_CONTAINER(window_showdata),box);
	box1 = gtk_vbox_new(FALSE,0);
	gtk_box_pack_start(GTK_BOX(box),box1,TRUE,TRUE,0);
	sep = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(box),sep,FALSE,FALSE,5);
	box2 = gtk_hbox_new(TRUE,0);
	gtk_box_pack_start(GTK_BOX(box),box2,FALSE,FALSE,0);
	
	//生成新滑动窗口并加入box1中
	scrlled_window = newScoll();
	gtk_box_pack_start(GTK_BOX(box1),scrlled_window,TRUE,TRUE,0);
	
	//生成查询按钮以及输入框并加入box2中
	label1 = gtk_label_new("数据库指令：");
    inqEntry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(inqEntry),"select * from proxyLog;");  //输入框默认文字
    gtk_box_pack_start(GTK_BOX(box2),label1,FALSE,FALSE,5);
    gtk_box_pack_start(GTK_BOX(box2),inqEntry,TRUE,TRUE,5);
	inqButton = gtk_button_new_with_label("查询");
	gtk_box_pack_start(GTK_BOX(box2),inqButton,FALSE,FALSE,5);
	g_signal_connect(G_OBJECT(inqButton),"clicked",G_CALLBACK(inquire),NULL);
	
	//生成清空按钮并加入box2中
	delButton = gtk_button_new_with_label("清空");
	gtk_box_pack_start(GTK_BOX(box2),delButton,FALSE,FALSE,5);
	g_signal_connect(G_OBJECT(delButton),"clicked",G_CALLBACK(delete),NULL);
	
	//生成刷新按钮并加入box2中
	freshButton=gtk_button_new_with_label("刷新");  
	gtk_box_pack_start(GTK_BOX(box2),freshButton,FALSE,FALSE,5);
	g_signal_connect(G_OBJECT(freshButton), "clicked",G_CALLBACK(fresh), NULL); 
	
	//生成关于按钮并加入box2中
	aboutButton=gtk_button_new_with_label("关于");  
	gtk_box_pack_start(GTK_BOX(box2),aboutButton,FALSE,FALSE,5);
	g_signal_connect(G_OBJECT(aboutButton), "clicked",G_CALLBACK(show_message), NULL); 
	
	gtk_widget_show_all(window_showdata);
}

//数据库展示界面
GtkWidget *show_list()
{
	GtkWidget* window;
	GtkWidget* frame;
	GtkWidget *scrlled_window;
	GtkWidget* box1;
    GtkWidget* box2;
	GtkWidget* sep;
	GtkWidget* label1;

	struct windowbox data;
	    
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(G_OBJECT(window),"destroy",G_CALLBACK(gtk_main_quit),window);
	gtk_window_set_title(GTK_WINDOW(window),"proxy-firewall");
	gtk_window_set_position(GTK_WINDOW(window),GTK_WIN_POS_CENTER);
	gtk_container_set_border_width(GTK_CONTAINER(window),40);
	
	//生成box，box1，box2
	box = gtk_vbox_new(FALSE,10);
	gtk_container_add(GTK_CONTAINER(window),box);
	box1 = gtk_vbox_new(FALSE,0);
	gtk_box_pack_start(GTK_BOX(box),box1,TRUE,TRUE,0);
	sep = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(box),sep,FALSE,FALSE,5);
	box2 = gtk_hbox_new(FALSE,0);
	gtk_box_pack_start(GTK_BOX(box),box2,FALSE,FALSE,0);
	
	//生成新滑动窗口并加入box1中
	scrlled_window = newScoll();
	gtk_box_pack_start(GTK_BOX(box1),scrlled_window,TRUE,TRUE,0);
	
	//生成查询按钮以及输入框并加入box2中
	label1 = gtk_label_new("数据库指令：");
    inqEntry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(box2),label1,FALSE,FALSE,5);
    gtk_box_pack_start(GTK_BOX(box2),inqEntry,TRUE,TRUE,5);
	inqButton = gtk_button_new_with_label("查询");
	gtk_box_pack_start(GTK_BOX(box2),inqButton,FALSE,FALSE,5);
	g_signal_connect(G_OBJECT(inqButton),"clicked",G_CALLBACK(inquire),NULL);

	//生成清空按钮并加入box2中
	delButton = gtk_button_new_with_label("清空");
	gtk_box_pack_start(GTK_BOX(box2),delButton,FALSE,FALSE,5);
	g_signal_connect(G_OBJECT(delButton),"clicked",G_CALLBACK(delete),NULL);

	//生成刷新按钮并加入box2中
	freshButton=gtk_button_new_with_label("刷新");  
	gtk_box_pack_start(GTK_BOX(box2),freshButton,FALSE,FALSE,5);
	g_signal_connect(G_OBJECT(freshButton), "clicked",G_CALLBACK(fresh), NULL); 
	
	//生成关于按钮并加入box2中
	aboutButton=gtk_button_new_with_label("关于");  
	gtk_box_pack_start(GTK_BOX(box2),aboutButton,FALSE,FALSE,5);
	g_signal_connect(G_OBJECT(aboutButton), "clicked",G_CALLBACK(show_message), NULL); 
	
	return window;
}

//防火墙功能启动
void beginwall()
{
	text=gtk_editable_get_chars(GTK_EDITABLE(shuru),0,-1);
	short port = 0;
	struct sockaddr_in cl_addr,proxyserver_addr;
	struct para main_para;	
	
	socklen_t sin_size = sizeof(struct sockaddr_in);
	int sockfd, accept_sockfd, on = 1;
	pthread_t Clitid;

	char ch[10];
	sprintf(ch, "%8s\n", text);   

	port = (short) atoi(ch);

	printf("Welcome to attend the experiments of designing a proxy firewall!  \n  use port %d  convert is %s \n",port,text);

	memset(&proxyserver_addr, 0, sizeof(proxyserver_addr));	// zero proxyserver_addr
	proxyserver_addr.sin_family = AF_INET;
	proxyserver_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	proxyserver_addr.sin_port = htons(port);

	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);// create socket
	if (sockfd < 0) {
		printf("Socket failed...Abort...\n");
		return;
	} 
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on));
	if (bind(sockfd, (struct sockaddr *) &proxyserver_addr, sizeof(proxyserver_addr)) < 0) {
		printf("Bind failed...Abort...\n");
		return;
	} 
	if (listen(sockfd, QUEUE_SIZE) < 0) {
		printf("Listen failed...Abort...\n");
		return;
	} 
	while (1) {
		accept_sockfd = accept(sockfd, (struct sockaddr *)&cl_addr, &sin_size); 	// block for connection request
		if (accept_sockfd < 0) {
			printf("accept failed");
			continue;
		}
		main_para.ip=(char*)inet_ntoa(cl_addr.sin_addr);
		main_para.socketfd=accept_sockfd;
		printf("accept %s\n",main_para.ip);
		if (1)
		{
					printf("make a thread! \n");
					pthread_create(&Clitid,NULL,(void*)dealonereq,&main_para);
		}
		else
			close(accept_sockfd);
			printf("closed! ...  \n ");
	}
	
	return ;
}

//启动线程，执行beginwall，并显示window_showdata窗口
void show()
{
	pthread_t wall;
	pthread_create(&wall,NULL,(void*)beginwall,NULL);
	gtk_widget_hide_all(window_in);
	gtk_widget_show_all(window_showdata); 	
}


//读取请求包内容
int getReqInfo(char* buf,char *hostname, int length,char* reqWay,char* reqProtocol)
{
	char *p=strstr(buf,"Host: ");
	int i=0,j=0,count=0,x=0,y=0;
	if(!p) {
		p=strstr(buf,"host: ");
	}
	bzero(hostname,256);
	for(i = (p-buf) + 6, j = 0; i<length; i++, j++)	{
		if(buf[i] =='\r') {
			hostname[j] ='\0';
			break;
		}
		else 
			hostname[j] = buf[i];
	}

	while(buf[count]!=' '){
		reqWay[count]=buf[count];
		count=count+1;
	}
	count=count+1;
	reqWay[count]='\0';

	while(x!=1){
		count=count+1;	
		if(buf[count]==' '){x=x+1;}
	}
	count=count+1;
	while(buf[count]!='\r'){
		reqProtocol[y]=buf[count];
		count=count+1;
		y=y+1;}
	reqProtocol[y]='\0';

	return 0;
}

//读取回复包的内容
int getRecInfo(char* buf, int length,char* recvProtocol,char* recvState,char* recvDate,char *recvcontent)
{
	int count=0,i=0,j=0,z=0,x=0,y=0;
	while(buf[count]!=' '){
		recvProtocol[i]=buf[count];
		i=i+1;
		count=count+1;	
	}
	count=count+1;
	recvProtocol[i+1]='\0';
	printf("-----recvProtocol %s \n",recvProtocol);
	if(recvProtocol=="")
	{
		recvState=recvProtocol;
		recvDate=recvProtocol;
		recvcontent=recvProtocol;
		return 0;
	}
	
	while(buf[count]!='\r'){
		recvState[j]=buf[count];
		j=j+1;
		count=count+1;	
	}
	count=count+1;
	recvState[j+1]='\0';
	printf("-----recvState is %s \n",recvState);
	
	char *p1=strstr(buf,"Date: ");
	if(!p1){recvDate="no Date";printf("-----1 \n");}
	else{
		for(x= (p1-buf) + 6, y = 0; x<length; x++, y++)	{
			if(buf[x] =='\r') {
				recvDate[y] ='\0';
				break;
			}
			else 
				recvDate[y] = buf[x];
		}
	}
	printf("-----2 \n");
	char *p2=strstr(buf,"Content-Type: ");
	if(!p2){recvcontent="no type";}
	else{
		for(x = (p2-buf) + 14; x<length; x++)	{
			if(buf[x] =='\r') {
				recvcontent[z] =',';
				z=z+1;
				break;
			}
			else {
				recvcontent[z] = buf[x];
				z=z+1;}
		}}
	printf("-----3 \n");
	char *p3=strstr(buf,"Content-Length: ");
	if(!p3){return 0;}
	else
		for(x = (p3-buf) + 16; x<length; x++)	{
				if(buf[x] =='\r') {
					recvcontent[z] ='\0';
					z=z+1;
					break;
				}
				else {
					recvcontent[z] = buf[x];
					z=z+1;
				}
	}
	printf("-----recvcontent  is %s \n",recvcontent );

	return 0;
	
}

GtkWidget *create_main_window() 
{
    GtkWidget *window;
	GtkWidget *vbox;  
	GtkWidget *test_button; 
	GtkWidget *label;    
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);  //创建子窗口
    gtk_window_set_title(GTK_WINDOW(window), "firewall");  

    vbox = gtk_vbox_new(FALSE, 0);  
    gtk_container_add(GTK_CONTAINER(window), vbox);  

	test_button=gtk_button_new_with_label("开始代理");  
	gtk_container_add(GTK_CONTAINER(vbox), test_button); 

	label = gtk_label_new("请输入代理端口号");  
    gtk_container_add(GTK_CONTAINER(vbox), label);  

	shuru=gtk_entry_new();//新建 输入框
	gtk_container_add(GTK_CONTAINER(vbox), shuru); 
	
	g_signal_connect(G_OBJECT(test_button), "clicked",G_CALLBACK(show), NULL); 
	g_signal_connect_swapped(G_OBJECT(window),"destroy",G_CALLBACK(gtk_main_quit), NULL);  

	return window;   
}

int main( int argc, char *argv[])  
{   
    gtk_init(&argc, &argv);  
  
    window_in=create_main_window();  //调用主窗口
    window_showdata=show_list();
    gtk_widget_show_all(window_in);  //显示主窗口
    gtk_main();  
  
    return 0;  
}

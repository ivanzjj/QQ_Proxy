#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <net/if.h>

#define ERR( str )	printf("%s",str)
#define SOCKS5_VERSION 0x05 

int listen_port = 8290 ; 
int connection_num = 10 ; 
int BUF_SIZE = 1024 * 2 ; 

int create_socket(){
	int sockfd ; 
	struct sockaddr_in serv ; 

	if( (sockfd = socket(AF_INET,SOCK_STREAM,0) ) < 0 ){
		ERR("socket error!\n");
		return -1 ; 
	} 
	memset( &serv , 0 , sizeof(serv) ) ;
	serv.sin_family = AF_INET ;
	serv.sin_addr.s_addr = htonl( INADDR_ANY ) ;
	serv.sin_port = htons( listen_port ) ; 
	
	if( bind( sockfd , (struct sockaddr *)(&serv) , sizeof(serv) ) < 0 ){
		ERR("bind failed!\n") ;
		return -1 ; 
	}
	if( listen( sockfd , connection_num ) < 0 ){
		ERR("connection failed!\n" );
		return -1 ;
	}
	return sockfd  ; 
}

int get_local_ip( char *ip ){
	int fd ; 
	char buf[100] ; 
	struct ifreq ifr ; 
	struct sockaddr_in *sa = NULL ; 
	fd = socket( AF_INET ,SOCK_STREAM , 0 ) ; 
	if( fd < 0 )	return 1 ;
	strcpy( ifr.ifr_name , "eth0" ) ;
	if( ioctl(fd , SIOCGIFADDR ,&ifr) < 0 )	return 1 ;
	sa = (struct sockaddr_in *)(&ifr.ifr_addr) ;
	strcpy( ip , inet_ntoa( sa->sin_addr ) );
	return 0  ;
}

int get_ip_from_host( char *hostname , char *ip ){
	struct hostent *hptr ; 
	char *p ; 
	hptr = gethostbyname( hostname ) ; 
	if( hptr == NULL || hptr->h_addr==NULL )	return 1 ; 
	p = inet_ntoa(*(struct in_addr*)hptr->h_addr)  ;
	memcpy( ip , p , (int)strlen(p) )  ;
	return 0 ; 
}

int relay(char *ch , int sz , struct sockaddr_in *r_addr ,int *offset){
	char ip[100] ; 
	int domain_len ; 
	char domain_name[100] ; 

	ch[sz] = 0 ;
	printf("RECV::%d\n" , sz );
	if( ch[0]!=0x00 && ch[1]!=0x00 ){
		ERR("request header 0x00 error!\n") ; 
		return 1 ; 
	}
	if( ch[2] != 0x00 ){
		ERR("request header flag error!\n" ) ; 
		return 1 ; 
	}
	memset( r_addr , 0 , sizeof(*r_addr) ) ; 
	r_addr->sin_family = AF_INET ;

	if( ch[3] == 0x01 ){
		memcpy( &(r_addr->sin_addr.s_addr) , ch+4 , 4 ) ;
		memcpy( &(r_addr->sin_port) , ch+8 , 2 ) ;
		*offset = 10 ;
	}
	else if(ch[3] == 0x03){
		domain_len = ch[4] ; 
		memcpy( domain_name , ch + 5 , domain_len ) ; 
		memcpy( &(r_addr->sin_port) , ch+5+domain_len , 2 ) ; 
		domain_name[ domain_len ] = 0 ; 
		printf("domain:%s\n" ,domain_name );
		if( get_ip_from_host( domain_name , ip ) ){
			ERR("get ip from host error!\n") ; 
			return 1;  
		}
		printf("domain:%s\n" , ip );
		r_addr->sin_addr.s_addr = inet_addr( ip ) ; 
		*offset = 4 + 1 + domain_len + 2 ; 
	}
	else{
		printf("ip-v6 is not supported yet!\n");
		return 1 ; 
	}
	return 0 ; 
}


int deal_with_udp_associate( int c_socket , char *buf ){
	int listen_socket , remote_socket ; 
	struct sockaddr_in listen_addr , toAddr ,r_addr ; 
	int nSize , sz , offset ; 
	int i ; 
	char local_ip[20] ; 

	memset( &listen_addr , 0 , sizeof( listen_addr ) ) ;
	memcpy( &(listen_addr.sin_port) , buf+8 , 2 ) ; 

	if( (listen_socket=socket( AF_INET , SOCK_DGRAM , 0 ) ) < 0 ){
		ERR("create udp socket error!\n")  ;
		return 1 ; 
	}
	listen_addr.sin_family = AF_INET ; 
	if( get_local_ip( local_ip ) ){
		ERR("get local ip error!\n") ;
		return 1 ; 
	} 
	listen_addr.sin_addr.s_addr = inet_addr( local_ip )  ; 
	if( bind( listen_socket , (struct sockaddr *)&listen_addr , sizeof(listen_addr) ) < 0 ){
		ERR("bind udp socket error!\n" ) ; 
		return 1 ;
	}
	printf("IJIJI\n");

	memcpy(buf , "\x05\x00\x00\x01" , 4 )  ;
	memcpy(buf+4 , &(listen_addr.sin_addr.s_addr) , 4 ) ; 
	memcpy(buf+8 , &(listen_addr.sin_port) , 2 ) ; 
	for(i=0;i<10;i++)
		printf("%X ",buf[i]);
	printf("\n");
	send( c_socket , buf , 10 , 0 )  ;
	close( c_socket  ) ; 
	nSize = sizeof( toAddr ) ;

	if( (remote_socket = socket(AF_INET , SOCK_DGRAM , 0 ) ) < 0 ) {
		ERR("create remote socket error!\n" ) ; 
		return 1;  
	}
	// udp request and reply  
	while( (sz = recvfrom(listen_socket,buf,BUF_SIZE,0,(struct sockaddr*)&toAddr,&nSize) )>0 ){
		if( relay( buf , sz , &r_addr , &offset ) ) {
			ERR("relay error!\n" ) ; 
			return 1 ;
		}
		printf("this is ip:%s port:%d\n",inet_ntoa(r_addr.sin_addr),ntohs(r_addr.sin_port) );
		for(i = 0; i < sz; i ++ )	printf("%X ",buf[i] );	printf("\n");
		printf("%d %d\n" , sz , offset);
		for(i = offset; i < sz; i ++ )	printf("%X ",buf[i] );	printf("\n");
		if( sendto( remote_socket , buf+offset , sz - offset , 0 , (struct sockaddr*)(&r_addr) , sizeof(r_addr) ) < 0 ) {
			ERR("sendto remote server error!\n" ) ; 
			break ; 
		} 
		printf("sended\n");
		nSize = sizeof( r_addr ) ; 
		if( (sz = recvfrom( remote_socket , buf + 10, BUF_SIZE , 0 ,(struct sockaddr*)(&r_addr) , &nSize ) ) < 0 ){
			ERR("recv from remote server error!\n") ; 
			break ; 
		}
		printf("reced!\n");
		printf("RRRRRRRRRRRR:%d\n" , sz );
		memcpy( buf , "\x00\x00\x00\x01" , 4 ) ; 
		memcpy( buf + 4 , &(r_addr.sin_addr.s_addr) , 4 )  ;
		memcpy( buf + 8 , &(r_addr.sin_port) , 2 ) ;
		if( sendto( listen_socket ,buf , sz + 10 , 0 , (struct sockaddr*)&toAddr, sizeof(toAddr) ) < 0 ){
			ERR("send to client error!\n" ) ; 
			break ; 
		} 
		nSize = sizeof( toAddr ) ; 
	}

	close( listen_socket ) ; 
	close( remote_socket ) ; 
	return 0 ; 
}

int sock5_auth( int c_socket ){
	char buf[ BUF_SIZE ] ; 
	int sz , nmethod ; 
	int i ;
	int s_socket = 0 ;
	struct sockaddr_in s_addr ; 
	struct hostent *hptr ;
	int s_addr_len ; 
	int connect_type ; 
	char *ip ; 

	if( recv( c_socket , buf , 2 , 0 ) <= 0 )	{printf("1\n");goto _err ;} 

	if( SOCKS5_VERSION != buf[0] )	{printf("2\n");goto _err ;}  
	nmethod = (int)buf[1] ; 
	if( recv( c_socket , buf , nmethod , 0 ) <= 0 )	{printf("3\n");goto _err ;}  
	
	if( buf[0] == 0x00 )
		strcpy( buf , "\x05\x00" ) ; 
	else if( buf[0] == 0x02 )
		strcpy( buf , "\x05\x02" ) ; 
	else{
		printf("unknown authorize method\n");
		strcpy( buf , "\x05\xFF" );  
		sz = (int)strlen( buf ) ; 
		send( c_socket , buf , sz , 0 ) ; 
		goto _err ; 
	}
	send( c_socket , buf , 2 , 0 ) ; 
	
	// request & reply 
	if( recv( c_socket , buf , 4 , 0 ) <= 0 )	{printf("4\n");goto _err ;} 
	if( SOCKS5_VERSION != buf[0] )	{printf("5\n");goto _err ;}  

	if( buf[1] == 0x01 ){
		// CONNECT 
		if( buf[3] == 0x01 ){
			// ip-v4
			connect_type = 1 ; 
			memset( &s_addr , 0 , sizeof(s_addr) ) ;
			s_addr.sin_family = AF_INET ;
			if( recv( c_socket ,buf , 4 , 0 ) <= 0 ){printf("6\n");goto _err ;}  
			memcpy( &(s_addr.sin_addr.s_addr) , buf , 4 ) ; 
			if( recv( c_socket , buf , 2 , 0 ) <= 0 ){printf("7\n");goto _err ;} 
			memcpy( &(s_addr.sin_port) , buf , 2 ) ;

		}
		else if( buf[3] == 0x03 ){
			// domain 
			connect_type = 3 ; 
			if( recv( c_socket , buf ,1 , 0 ) <= 0 ){printf("8\n");goto _err ;}  
			sz = (int)buf[0] ; 
			if( recv( c_socket , buf , sz , 0 ) <= 0 ){printf("9\n");goto _err ;}  
			buf[ sz ] = 0 ; 
			memset( &s_addr , 0 , sizeof( s_addr ) ) ;
			s_addr.sin_family = AF_INET ;
			
			hptr = gethostbyname( buf ) ; 
			if( hptr==NULL || hptr->h_addr==NULL){printf("10\n");goto _err ;}  
			memcpy( &(s_addr.sin_addr.s_addr) , hptr->h_addr , 4 ) ; 
			ip = inet_ntoa(*((struct in_addr *)hptr->h_addr))  ;
			if( recv( c_socket, buf , 2 , 0 ) <= 0 ){printf("11\n");goto _err ;}  
			memcpy( &(s_addr.sin_port) , buf , 2 )  ;
			buf[ 2 ] = 0 ;
		} 
		else if( buf[3] == 0x04 ){
			// ip-v6 
			if( recv( c_socket , buf , 16 , 0 ) <= 0 ){printf("12\n");goto _err ;}  
			printf("ip-v6:%s\n", buf );
			if( recv( c_socket , buf , 2 , 0 ) <= 0 ){printf("13\n");goto _err ;}   
			printf("ip-v6:%s\n" , buf );
			strcpy( buf , "\x05\x08\x00\x04" ) ;
			send( c_socket, buf , 4, 0 ) ; 
			{printf("14\n");goto _err ;}  
		}
		else{printf("15\n");goto _err ;}   
		
		if( (s_socket = socket( AF_INET , SOCK_STREAM , 0 ) ) < 0 ){
			ERR("socket initial failed!\n" ) ;
			{printf("16\n");goto _err ;}   
		}
		s_addr_len = sizeof( s_addr )  ;
		if( connect( s_socket ,(struct sockaddr *)(&s_addr) , s_addr_len ) < 0 ){
			ERR("connect remote socket failed!\n") ;
			{printf("17\n");goto _err ;}   
		} 
		s_addr_len = sizeof( s_addr ) ; 
		if( getpeername( s_socket ,(struct sockaddr*)(&s_addr) ,&s_addr_len) < 0 ){
			ERR("getpeername failed!\n") ;
			{printf("18\n");goto _err ;}   
		}
		if( connect_type == 1 )	
			memcpy( buf , "\x05\x00\x00\x01" , 4) ; 
		else
			memcpy( buf , "\x05\x00\x00\x03" , 4) ; 
		memcpy( buf+4 , &(s_addr.sin_addr.s_addr) , 4 ) ;
		memcpy( buf+8 , &(s_addr.sin_port) , 2) ; 
		send( c_socket , buf , 10 , 0 ) ; 
		return s_socket ;
	}//end of if( buf[0] == 0x01 )
	else if( buf[1] == 0x02 ){
		// BIND ;
		{printf("19\n");goto _err ;}  
	}
	else{
		// UDP ASSOCIATE 
		// TODO 
		if( buf[3] == 0x01 ){
			printf("UDP ASSOCIATE!\n");
			memcpy( buf , "\x05\x00\x00\x01" , 4) ;
			memset( &s_addr , 0 , sizeof(s_addr) ) ;
			if( recv( c_socket , buf+4 , 4 , 0 ) <= 0 )	goto _err ; 
			printf("%X %X %X %X\n" , buf[4] , buf[5] ,buf[6] , buf[7] );
			if( buf[4]==0x00 && buf[5]==0x00 && buf[6]==0x00 && buf[7]==0x00 ){
				if( recv( c_socket, buf+8,2,0 ) <= 0 )	goto _err ; 
				deal_with_udp_associate( c_socket , buf ) ; 
				return -1 ; 
			}
		}
		else if( buf[3] == 0x03 ){
			printf("NO DEfine!\n");
		}
		else{
			ERR("UDP ASSOCIATE CAN not support ip-v6 yet!\n") ;
			goto _err ; 
		}
		{printf("20\n");goto _err ;}  
	}

_err :
	if( s_socket != 0 )	close( s_socket ) ; 
	return -1 ; 
}

int deal_with_client( int c_socket ){
	int s_socket ;
	char buf[ BUF_SIZE ] ; 
	int buf_size ; 

	if( (s_socket=sock5_auth( c_socket )) < 0 ){
		ERR("sock5 authorize failed!\n") ;
		close( c_socket ) ;
		return 1; 
	}
	if( fork() == 0 ){
		while( (buf_size = recv(c_socket,buf,BUF_SIZE,0) ) > 0 ){
			buf[ buf_size ] = 0 ; 
			if( send( s_socket , buf , buf_size , 0 ) <= 0 )	break ; 
		}
		shutdown( c_socket , SHUT_RDWR ) ;
		close( c_socket ) ; 
		shutdown( s_socket , SHUT_RDWR ) ; 
		close( s_socket ) ; 
		exit( 0 ) ; 
	}
	if( fork() == 0 ){
		while( (buf_size = recv(s_socket,buf,BUF_SIZE,0) ) > 0 ){
			buf[ buf_size ] = 0 ;
			if( send( c_socket , buf , buf_size , 0 ) <= 0 )	break ; 
		}
		shutdown( c_socket , SHUT_RDWR ) ;
		close( c_socket ) ; 
		shutdown( s_socket , SHUT_RDWR ) ; 
		close( s_socket ) ; 
		exit( 0 ) ; 
	}
	close( s_socket ) ; 
	close( c_socket ) ; 
	return 0 ; 
}

void handle_sigchld(){
	while( waitpid(-1,NULL,WNOHANG)<=0 ) ;  
}

int main(){
	int sockfd , c_sockfd ;
	struct sockaddr_in c_addr ; 
	int c_addr_len ; 
	
	signal( SIGCHLD , handle_sigchld ) ; 
	if( (sockfd = create_socket()) < 0 ){
		ERR(" create socket failed!\n" ) ; 
		return 1 ; 
	}
	while( 1 ) {
		c_addr_len = sizeof( c_addr )  ;
		if( (c_sockfd = accept( sockfd , (struct sockaddr *)(&c_addr) , &c_addr_len))<0){
			ERR("accept new connection error!\n") ; 
			return 1; 
		}
		if( fork() == 0 ){
			close( sockfd ) ; 
			deal_with_client( c_sockfd ) ; 
			exit( 0 ) ;
		}
		close( c_sockfd ) ; 
	}
	return 0 ; 
}


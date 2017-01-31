//IMPORTANT: buffer overflow was coming because we were putting disconnected fd's in the select statement.

#include "axel.h"

/* Axel */
static void save_state( axel_t *axel );
static void *setup_thread( void * );
static void axel_message( axel_t *axel, char *format, ... );
static void axel_divide( axel_t *axel );
static void axel_divide_half(axel_t *axel,int index);
int array_select[MAX_NUM_CONNECTIONS];
static char *buffer = NULL;
static int should_disconnect=0;
static pthread_cond_t sel_cond;	
static pthread_mutex_t alloc=PTHREAD_MUTEX_INITIALIZER;
int array_close[MAX_NUM_CONNECTIONS];
/*
static int num_tcp_conn=0;
static int allowed_conn=4;
static int del_conn=0;
*/
/* Create a new axel_t structure

					*/
axel_t *axel_new( conf_t *conf, int count, void *url )
{
	search_t *res;
	axel_t *axel;
	url_t *u;
	char *s;
	int i;
	printf("\n inisde axel structure\n");
	axel = malloc( sizeof( axel_t ) );
	memset( axel, 0, sizeof( axel_t ) );
	*axel->conf = *conf;
	axel->conn = malloc( sizeof( conn_t ) * axel->conf->num_connections );
	memset( axel->conn, 0, sizeof( conn_t ) * axel->conf->num_connections );
	if( axel->conf->max_speed > 0 )
	{
		if( (float) axel->conf->max_speed / axel->conf->buffer_size < 0.5 )
		{
			if( axel->conf->verbose >= 2 )
				axel_message( axel, _("Buffer resized for this speed.") );
			axel->conf->buffer_size = axel->conf->max_speed;
		}
		axel->delay_time = (int) ( (float) 1000000 / axel->conf->max_speed * axel->conf->buffer_size * axel->conf->num_connections );
	}
	if( buffer == NULL )
		buffer = malloc( max( MAX_STRING, axel->conf->buffer_size ) );
	
	if( count == 0 )
	{
		axel->url = malloc( sizeof( url_t ) );
		axel->url->next = axel->url;
		strncpy( axel->url->text, (char *) url, MAX_STRING );
	}
	else
	{
		res = (search_t *) url;
		u = axel->url = malloc( sizeof( url_t ) );
		for( i = 0; i < count; i ++ )
		{
			strncpy( u->text, res[i].url, MAX_STRING );
			if( i < count - 1 )
			{
				u->next = malloc( sizeof( url_t ) );
				u = u->next;
			}
			else
			{
				u->next = axel->url;
			}
		}
	}
	
	axel->conn[0].conf = axel->conf;
	if( !conn_set( &axel->conn[0], axel->url->text ) ) //parsing the url and initializing conn[0] . 
	{
		axel_message( axel, _("Could not parse URL.\n") );
		axel->ready = -1;
		return( axel );
	}


	//static variables put into axel structure by manmeet singh.
	axel->del_conn=0;
	axel->allowed_conn=axel->conf->num_connections;
	axel->num_tcp_conn=0;

	axel->conn[0].local_if = axel->conf->interfaces->text;
	axel->conf->interfaces = axel->conf->interfaces->next;
	
	strncpy( axel->filename, axel->conn[0].file, MAX_STRING );
	http_decode( axel->filename );
	if( *axel->filename == 0 )	/* Index page == no fn		*/
		strncpy( axel->filename, axel->conf->default_filename, MAX_STRING );
	if( ( s = strchr( axel->filename, '?' ) ) != NULL && axel->conf->strip_cgi_parameters )
		*s = 0;		/* Get rid of CGI parameters		*/
	
	if( !conn_init( &axel->conn[0] ) )
	{
		axel_message( axel, axel->conn[0].message );
		axel->ready = -1;
		printf("\n connection couldn't be established***");
		return( axel );
	}
        else 
        {
		printf("\n connection initialized(in axel_new())\n");
		
		axel->conn[0].deliberate_close=0;
		
	}
	
	/* This does more than just checking the file size, it all depends
	   on the protocol used.					*/
	if( !conn_info( &axel->conn[0] ) )  //gets information about the file size.
	{
		axel_message( axel, axel->conn[0].message );
		axel->ready = -1;
		return( axel );
	}
        else printf("\n info obtained\n");
	s = conn_url( axel->conn );
	strncpy( axel->url->text, s, MAX_STRING );
	if( ( axel->size = axel->conn[0].size ) != INT_MAX )
	{
		if( axel->conf->verbose > 0 )
			axel_message( axel, _("File size: %lld bytes"), axel->size );
                        printf("File size: %lld bytes",axel->size);
	}
	
	/* Wildcards in URL --> Get complete filename			*/
	if( strchr( axel->filename, '*' ) || strchr( axel->filename, '?' ) )
		strncpy( axel->filename, axel->conn[0].file, MAX_STRING );
	
	return( axel );
}

/* Open a local file to store the downloaded data			*/
int axel_open( axel_t *axel )
{
	int i, fd;
	long long int j;
	printf("Opening output file %s",axel->filename);
	if( axel->conf->verbose > 0 )
		{axel_message( axel, _("Opening output file %s"), axel->filename );
                
                }
	snprintf( buffer, MAX_STRING, "%s.st", axel->filename );
	
	axel->outfd = -1;
	
	/* Check whether server knows about RESTart and switch back to
	   single connection download if necessary			*/
	if( !axel->conn[0].supported )
	{
		axel_message( axel, _("Server unsupported, "
			"starting from scratch with one connection.") );
		axel->conf->num_connections = 1;
		axel->conn = realloc( axel->conn, sizeof( conn_t ) );
		axel_divide( axel );
                printf("\n divided size");
	}
	else if( ( fd = open( buffer, O_RDONLY ) ) != -1 )
	{
		read( fd, &axel->conf->num_connections, sizeof( axel->conf->num_connections ) );
		
		axel->conn = realloc( axel->conn, sizeof( conn_t ) * axel->conf->num_connections );
		memset( axel->conn + 1, 0, sizeof( conn_t ) * ( axel->conf->num_connections - 1 ) );

		axel_divide( axel );
		
		read( fd, &axel->bytes_done, sizeof( axel->bytes_done ) );
		for( i = 0; i < axel->conf->num_connections; i ++ )
			read( fd, &axel->conn[i].currentbyte, sizeof( axel->conn[i].currentbyte ) );

		axel_message( axel, _("State file found: %lld bytes downloaded, %lld to go."),
			axel->bytes_done, axel->size - axel->bytes_done );
		
		close( fd );
		
		if( ( axel->outfd = open( axel->filename, O_WRONLY, 0666 ) ) == -1 )
		{
			axel_message( axel, _("Error opening local file") );
                        printf("\n returning axel_open: error opening local file\n");
			return( 0 );
		}
	}

	/* If outfd == -1 we have to start from scrath now		*/
	if( axel->outfd == -1 )
	{
		axel_divide( axel );
		
		if( ( axel->outfd = open( axel->filename, O_CREAT | O_WRONLY, 0666 ) ) == -1 )
		{
			axel_message( axel, _("Error opening local file") );
                        printf("\n returning axel_open: error opening local file\n");
			return( 0 );
		}
		
		/* And check whether the filesystem can handle seeks to
		   past-EOF areas.. Speeds things up. :) AFAIK this
		   should just not happen:				*/
		if( lseek( axel->outfd, axel->size, SEEK_SET ) == -1 && axel->conf->num_connections > 1 )
		{
			/* But if the OS/fs does not allow to seek behind
			   EOF, we have to fill the file with zeroes before
			   starting. Slow..				*/
			axel_message( axel, _("Crappy filesystem/OS.. Working around. :-(") );
			lseek( axel->outfd, 0, SEEK_SET );
			memset( buffer, 0, axel->conf->buffer_size );
			j = axel->size;
			while( j > 0 )
			{
				write( axel->outfd, buffer, min( j, axel->conf->buffer_size ) );
				j -= axel->conf->buffer_size;
			}
		}
	}
	printf("\n returning axel_open\n");
	return( 1 );
}

/* Start downloading							*/
void axel_start( axel_t *axel )
{
	int i;
	
	/* HTTP might've redirected and FTP handles wildcards, so
	   re-scan the URL for every conn				*/
	for( i = 0; i < axel->conf->num_connections; i ++ )
	{
		conn_set( &axel->conn[i], axel->url->text );
		axel->url = axel->url->next; // ** here we are parsing the url into
		axel->conn[i].local_if = axel->conf->interfaces->text;
		axel->conf->interfaces = axel->conf->interfaces->next;
		axel->conn[i].conf = axel->conf;
		axel->conn[i].enabled=0;
		axel->conn[i].deliberate_close=0;
		axel->conn[i].fd=-1;
		axel->conn[i].index=i;
		axel->conn[i].increase_conn=-1;
		if( i ) axel->conn[i].supported = 1; //???supported checks whether server supports multiple downloads or not or it supports resuming.
	}
	
	if( axel->conf->verbose > 0 )
		axel_message( axel, _("Starting download") );
	
	for( i = 0; i < axel->conf->num_connections; i ++ )
	if( axel->conn[i].currentbyte <= axel->conn[i].lastbyte )
	{
		if( axel->conf->verbose >= 2 )
		{
			axel_message( axel, _("Connection %i downloading from %s:%i using interface %s"),
		        	      i, axel->conn[i].host, axel->conn[i].port, axel->conn[i].local_if );
		}
		
		axel->conn[i].state = 1;
		axel->num_tcp_conn++;
		if( pthread_create( axel->conn[i].setup_thread, NULL, setup_thread, &axel->conn[i] ) != 0 )
		{
			axel_message( axel, _("pthread error!!!") );
			axel->ready = -1;
		}
		else
		{
			axel->conn[i].last_transfer = gettime();
		}
	}
	
	/* The real downloading will start now, so let's start counting	*/
	axel->start_time = gettime();
	axel->ready = 0;


	/*Condition variable for select*/
	
	memset(array_close,0,sizeof(int)*MAX_NUM_CONNECTIONS);

	int ret = pthread_cond_init(&sel_cond, NULL);
}

/* Main 'loop'								*/
void axel_do( axel_t *axel )
{
	fd_set fds[1];
	int hifd, i;
	long long int remaining,size;
	struct timeval timeval[1];
	//printf("\n in axel_do **\n");
	//axel_messsage(axel,"in axel_do");
	
	/* Create statefile if necessary
				*/
	int index=0;
	//for(index=0;index<axel->conf->num_connections;index++)
//if( axel->conn[index].currentbyte < axel->conn[index].lastbyte&&axel->conn[index].deliberate_close==1)
//if(axel->conn[index].enabled==0)
//printf("\nconn: %d , enabled:%d ,remaining: %d ,delib:%d, state: %d, select_array %d\n\n",index,axel->conn[index].enabled,axel->conn[index].lastbyte-axel->conn[index].currentbyte,axel->conn[index].deliberate_close,axel->conn[index].state,array_select[index]);
/*	if( gettime() > axel->next_state )
	{
		save_state( axel );
		axel->next_state = gettime() + axel->conf->save_state_interval;  //which is 10 seconds in config file.
	}*/
	int pq=axel->allowed_conn-axel->num_tcp_conn;
	long long int remaining_size=0;
	/*if(pq!=0)
	{
		printf("tcp_conn:%d	pq:%d	del_close:%d\n,",axel->num_tcp_conn,pq,axel->del_conn);
	}*/
	if(pq<0)
	{
	  	int conn_to_disconn=(-1)*pq;
	  	//printf("waiting to disconnect %d num of conns\n",conn_to_disconn);
	  	//axel_message(axel,_("num of tcp conns %i"),axel->num_tcp_conn);
	
	  	//axel_message(axel,_("gng 2 disconn %i num conns"),conn_to_disconn);
	
	  	int p=axel->num_tcp_conn;
		//printf("__**here %d, %d",conn_to_disconn,axel->num_tcp_conn);
	
	
		index=0;

		//printf("waiting to disconnect %d num of conns and present**:%d\n",conn_to_disconn,axel->num_tcp_conn);
		while(conn_to_disconn>0&&index<axel->conf->num_connections)
		{
			remaining_size=(axel->conn[index].lastbyte - axel->conn[index].currentbyte) + 1;
			printf("*******************inside deliberately closed %d*********************\n",axel->conf->num_connections);
			//printf("waiting to disconnect %d num of conns and present\n",conn_to_disconn,axel->num_tcp_conn);
			if(remaining_size>=1&&(axel->conn[index].deliberate_close==0))
			{
				
				int l=-9;
				if(axel->conn[index].state==1)
				{
					while(axel->conn[index].state!=0)
					{
						if( gettime() > axel->conn[index].last_transfer + axel->conf->reconnect_delay )
						{
							if ( *axel->conn[index].setup_thread != 0 )	
								pthread_cancel( *axel->conn[index].setup_thread );
							else
								{axel->conn[index].state = 0;break;}
							axel->conn[index].state = 0;
							break;
						}
					}
					if ( *axel->conn[index].setup_thread != 0 )
						l=pthread_join(*(axel->conn[index].setup_thread), NULL);
					
				}
				else
				{
					if ( *axel->conn[index].setup_thread != 0 )
						l=pthread_join(*(axel->conn[index].setup_thread), NULL);
						//printf("waiting to disconnect\n");
				}
				array_close[index]=1;
				/*printf("waiting for select thread to disconnect conn:%d\n",index);
				pthread_mutex_lock(&sel_mutex);
				while(array_close[index]!=2)
					
				{
					//printf("waiting to disconnect conn %d\n",index);   //???ask sir about starvtion problem.
					pthread_cond_wait(&sel_cond,&sel_mutex);
				}
				array_close[index]=0;
				pthread_mutex_unlock(&sel_mutex);
				printf("woken up\n");*/
				
				axel->conn[index].deliberate_close=1;
				axel->conn[index].state=0;
				axel->num_tcp_conn --;
				//printf("\n thread_id:%lld and state %d and flag %d and index %d\n",*axel->conn[index].setup_thread,axel->conn[index].state,l,index);
				axel_message(axel,_("num of tcp conns %i"),axel->num_tcp_conn);
				//conn_disconnect(&axel->conn[index]);
				axel->del_conn++;
				*(axel->conn[index].setup_thread)=0;
				
				conn_to_disconn--;
				
						
			}
			index++;
			
			
		}
	}
	
	else if((pq>0)&&(axel->del_conn==0))
	{

		
		
		int index=0;
		printf("*******************inside axel divide*********************\n");
		int temp_conf_conn=axel->conf->num_connections;//since the number of conns will increase if i divide the connections.
		while(pq>0&&index<temp_conf_conn)
		{
			remaining_size=(axel->conn[index].lastbyte - axel->conn[index].currentbyte) + 1;
	
			if((remaining_size>=20000)&&(axel->conn[index].lastbyte>=0)&&(axel->conn[index].increase_conn==-1)&&(array_close[index]==0))
			{
				printf("\n calling axel divide on index %d\n ",index);
				axel_divide_half(axel,index);
				pq--;
						
				
				
			}
			index++;
			//printf("\n stuck in a loop\n");
			
		}

		

	}


/*

	select will return ready fd's. 2 cases possible:
	
	1. Thread chosen to be disconnected is in the fd_set that contains ready fd's

	2. Thread chosen to be disconnected is in the select call.
	   We need to wait for select to return as it is specified that closing an fd in other thread may not have any effect on the select call

	3. Thread chosen to be disconnected is already disconnected.
	   This case should not cause any problem as I am simply going to diconnect this connection and it shall be read later on after 	   its initiation.


*/

	/*
	FD_ZERO( fds );
	hifd = 0;
	//printf("\n num of conf conn %d\n",axel->conf->num_connections);
	for( i = 0; i < axel->conf->num_connections; i ++ )
	{
		
		if( axel->conn[i].enabled && (axel->conn[i].deliberate_close==0) )
			{FD_SET( axel->conn[i].fd, fds ); 
			//printf("**yes\n");
			}
		hifd = max( hifd, axel->conn[i].fd );
		
	
	}
	printf("\n allowed conn %d\n",axel->allowed_conn);
	if( hifd == 0 )
	{
		
		//printf("\n here---sleep");
		usleep( 100000 );
		
		goto conn_check;
	}
	else
	{
		
		timeval->tv_sec = 0;
		timeval->tv_usec = 100000;
		
		if( select( hifd + 1, fds, NULL, NULL, timeval ) == -1 )
		{
			axel->ready = -1;
			
			return;
		}
		else
		printf("select() success\n");
	}
	
	

	//if(FD_ISSET( axel->conn[2].fd, fds )) printf("\n yes for 3\n"); else printf("\n no for 3\n"); 
	
	for( i = 0; i < axel->conf->num_connections; i ++ )
	if( axel->conn[i].enabled&&(axel->conn[i].fd>0) ) {
	printf("\n calling fd_isset %d\n",i);
	if( FD_ISSET( axel->conn[i].fd, fds ) )
	{
		
		//printf("handling connection %d\n",i);		
		axel->conn[i].last_transfer = gettime();
		size = read( axel->conn[i].fd, buffer, axel->conf->buffer_size );
		printf("\n size is %d\n",size);
		if( size == -1 )
		{
			if( axel->conf->verbose )
			{
				axel_message( axel, _("Error on connection %i! "
					"Connection closed"), i );
			}
			axel->conn[i].enabled = 0;
			conn_disconnect( &axel->conn[i] );
			continue;
		}
		else if( size == 0 )
		{
			if( axel->conf->verbose )
			{
				//Only abnormal behaviour if:		
				if( axel->conn[i].currentbyte < axel->conn[i].lastbyte && axel->size != INT_MAX )
				{
					axel_message( axel, _("Connection %i unexpectedly closed"), i );
				}
				else
				{
					axel_message( axel, _("Connection %i finished"), i );
					axel->num_tcp_conn --;
				}
			}
			if( !axel->conn[0].supported )
			{
				axel->ready = 1;
			}
			axel->conn[i].enabled = 0;
			conn_disconnect( &axel->conn[i] );
			continue;
		}
		// remaining == Bytes to go					//
		remaining = axel->conn[i].lastbyte - axel->conn[i].currentbyte + 1;
		if( remaining < size )
		{
			if( axel->conf->verbose )
			{
				axel_message( axel, _("Connection %i finished"), i );
			}
			axel->conn[i].enabled = 0;
			axel->num_tcp_conn --;
			conn_disconnect( &axel->conn[i] );
			size = remaining;
			// Don't terminate, still stuff to write!	//
		}
		// This should always succeed..				//
		lseek( axel->outfd, axel->conn[i].currentbyte, SEEK_SET );
		if( write( axel->outfd, buffer, size ) != size )
		{
			
			axel_message( axel, _("Write error!") );
			axel->ready = -1;
			return;
		}
		axel->conn[i].currentbyte += size;
		axel->bytes_done += size;
	}
	else
	{
		if( gettime() > axel->conn[i].last_transfer + axel->conf->connection_timeout )
		{
			if( axel->conf->verbose )
				axel_message( axel, _("Connection %i timed out"), i );
			conn_disconnect( &axel->conn[i] );
			axel->conn[i].enabled = 0;
		}
	}}*/
	
	if( axel->ready==1 )
		return;
	
	
conn_check:
	/* Look for aborted connections and attempt to restart them.	*/
	for( i = 0; i < axel->conf->num_connections; i ++ )
	{
//printf("\nconn: %d , enabled:%d ,current: %d,last:%d  ,delib:%d, state: %d\n\n\n",i,axel->conn[i].enabled,axel->conn[i].currentbyte,axel->conn[i].lastbyte,axel->conn[i].deliberate_close,axel->conn[i].state);
		//printf("__**__here\n");
		if( !axel->conn[i].enabled && (axel->conn[i].currentbyte < axel->conn[i].lastbyte)&& (axel->conn[i].deliberate_close==0) &&(array_close[i]==0)&&(axel->conn[i].currentbyte>=0))
		{
			if( axel->conn[i].state == 0 )
			{	
				// Wait for termination of this thread
			printf("\n handling closed connections %d currentbyte:%d\n",i,axel->conn[i].currentbyte);
				if ( *axel->conn[i].setup_thread != 0 )
					pthread_join(*(axel->conn[i].setup_thread), NULL);
				
				conn_set( &axel->conn[i], axel->url->text );
				axel->url = axel->url->next;
				/* axel->conn[i].local_if = axel->conf->interfaces->text;
				axel->conf->interfaces = axel->conf->interfaces->next; */
				if( axel->conf->verbose >= 2 )
					axel_message( axel, _("Connection %i downloading from %s:%i using interface %s"),
				        	      i, axel->conn[i].host, axel->conn[i].port, axel->conn[i].local_if );
				
				axel->conn[i].state = 1;
				
				if( pthread_create( axel->conn[i].setup_thread, NULL, setup_thread, &axel->conn[i] ) == 0 )
				{
					axel->conn[i].last_transfer = gettime();
				}
				else
				{
					axel_message( axel, _("pthread error!!!") );
					axel->ready = -1;
				}
			}
			else
			{
				if( gettime() > axel->conn[i].last_transfer + axel->conf->reconnect_delay )
				{
					if ( *axel->conn[i].setup_thread != 0 )	
						pthread_cancel( *axel->conn[i].setup_thread );
					axel->conn[i].state = 0;
				}
			}
		}
	else if( !axel->conn[i].enabled && (axel->conn[i].currentbyte < axel->conn[i].lastbyte)&& (axel->conn[i].deliberate_close==1) &&array_close[i]==0)
		{
	
			if(axel->allowed_conn>axel->num_tcp_conn)
			{
				printf("\n handling deliberately closed connections %d\n",i);
				axel->conn[i].deliberate_close=0;
				axel->num_tcp_conn++;
			 	if( axel->conn[i].state == 0 )
			         {	
				// Wait for termination of this thread
				if ( *axel->conn[i].setup_thread != 0 )
					pthread_join(*(axel->conn[i].setup_thread), NULL);
				
				conn_set( &axel->conn[i], axel->url->text );
				axel->url = axel->url->next;
				/* axel->conn[i].local_if = axel->conf->interfaces->text;
				axel->conf->interfaces = axel->conf->interfaces->next; */
				if( axel->conf->verbose >= 2 )
					axel_message( axel, _("Connection %i downloading from %s:%i using interface %s"),
				        	      i, axel->conn[i].host, axel->conn[i].port, axel->conn[i].local_if );
				
				axel->conn[i].state = 1;
				axel->conn[i].deliberate_close=0;
				axel->del_conn--;
				//printf("\n handled deliberately disconnected connection :%d\n",i);
				axel_message(axel,_("handled delibrtly disconn conn :%i"),i);
				if( pthread_create( axel->conn[i].setup_thread, NULL, setup_thread, &axel->conn[i] ) == 0 )
				{
					axel->conn[i].last_transfer = gettime();
				}
				else
				{
					axel_message( axel, _("pthread error!!!") );
					axel->ready = -1;
				}
			       }
			else
			     {
				if( gettime() > axel->conn[i].last_transfer + axel->conf->reconnect_delay )
				{
					if ( *axel->conn[i].setup_thread != 0 )					
						pthread_cancel( *axel->conn[i].setup_thread );
					axel->conn[i].state = 0;
				}
			     }
			  
			}
		}

			
	}
	

	

}

/* Close an axel connection						*/
void axel_close( axel_t *axel )
{
	int i;
	message_t *m;
	
	/* Terminate any thread still running				*/
	for( i = 0; i < axel->conf->num_connections; i ++ )
		/* don't try to kill non existing thread */
		if ( *axel->conn[i].setup_thread != 0 )
			pthread_cancel( *axel->conn[i].setup_thread );
	
	/* Delete state file if necessary				*/
	if( axel->ready == 1 )
	{
		snprintf( buffer, MAX_STRING, "%s.st", axel->filename );
		unlink( buffer );
	}
	/* Else: Create it.. 						*/
	else if( axel->bytes_done > 0 )
	{
		save_state( axel );
	}

	/* Delete any message not processed yet				*/
	while( axel->message )
	{
		m = axel->message;
		axel->message = axel->message->next;
		free( m );
	}
	
	/* Close all connections and local file				*/
	close( axel->outfd );
	for( i = 0; i < axel->conf->num_connections; i ++ )
		conn_disconnect( &axel->conn[i] );

	free( axel->conn );
	free( axel );
}

/* time() with more precision						*/
double gettime()
{
	struct timeval time[1];
	
	gettimeofday( time, 0 );
	return( (double) time->tv_sec + (double) time->tv_usec / 1000000 );
}

/* Save the state of the current download				*/
void save_state( axel_t *axel )
{
	int fd, i;
	char fn[MAX_STRING+4];

	/* No use for such a file if the server doesn't support
	   resuming anyway..						*/
	if( !axel->conn[0].supported )
		return;
	
	snprintf( fn, MAX_STRING, "%s.st", axel->filename );
	if( ( fd = open( fn, O_CREAT | O_TRUNC | O_WRONLY, 0666 ) ) == -1 )
	{
		return;		/* Not 100% fatal..			*/
	}
	write( fd, &axel->conf->num_connections, sizeof( axel->conf->num_connections ) );
	write( fd, &axel->bytes_done, sizeof( axel->bytes_done ) );
	for( i = 0; i < axel->conf->num_connections; i ++ )
	{
		write( fd, &axel->conn[i].currentbyte, sizeof( axel->conn[i].currentbyte ) );
	}
	close( fd );
}

/* Thread used to set up a connection					*/
void *setup_thread( void *c )
{
	conn_t *conn = (conn_t *)c;
	int oldstate;
	
	/* Allow this thread to be killed at any time.			*/
	pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, &oldstate );
	pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, &oldstate );
	
	if( conn_setup( conn ) )
	{
		conn->last_transfer = gettime();
		if( conn_exec( conn ) )
		{
			conn->last_transfer = gettime();
			conn->enabled = 1;
			conn->state = 0;
			array_select[conn->index]=conn->fd;
			*conn->setup_thread=0;
			//printf("making index %d positive=%d\n",conn->index,conn->fd);
			return( NULL );
		}
	}
	
	conn_disconnect( conn );
	conn->enabled = 0;
	conn->state = 0;
	printf("\nProblem in establishing connection %d\n",conn->index);
	*conn->setup_thread=0;
	return( NULL );
}
void *select_thread(void *c)
{

	printf("\n inside select thread");

	time_t start_time=gettime();
	fd_set fds[1];
	axel_t *axel=(axel_t *)c;
	int hifd, i,idle,live;
	FILE * data_fp=fopen("data_dynamic.txt","w");
	long long int remaining,size;
	struct timeval timeval[1];
	/* Wait for data on (one of) the connections			*/
	while(axel->ready!=1)
	{
	
	
		int j=0;
		idle=0;
		live=0;
		
		for(j=0;j<axel->conf->num_connections;j++)
		{
			if(array_close[j]==1)
			{
			
				int data,size1;
				if(axel->conn[j].fd>0)
				{
				
						while((size1 = recv( axel->conn[j].fd, buffer, axel->conf->buffer_size,MSG_DONTWAIT ))>0)
						{
							printf("\nselect thread: data:%d reading left data:%d from conn:%d which is going to be disconnected\n",data,size1,j);
							
							if(size1>0)
							{
								int remaining = axel->conn[j].lastbyte - axel->conn[j].currentbyte + 1;
								if( remaining < size1 )
								{
					
									axel_message( axel, _("Connection %i finished"), j );
						
									axel->conn[j].enabled = 0;
									axel->num_tcp_conn --;
									conn_disconnect(&axel->conn[j]);
									size1 = remaining;
						
								}
								lseek( axel->outfd, axel->conn[j].currentbyte, SEEK_SET );
								if( write( axel->outfd, buffer, size1 ) != size1 )
								{
					
									axel_message( axel, _("Write error!") );
									axel->ready = -1;
									while(1) printf("write error");
								}
								axel->conn[j].currentbyte += size1;
								axel->bytes_done += size1;
								if(remaining < size1) break;
								
							}
							else
								break;
							
							
						}
					
				}
				array_select[j]=-1;
				conn_disconnect(&axel->conn[j]);
				if( axel->bytes_done == axel->size )
				{
					printf("DEBUGGING1*: MAKING AXEL->READY=1");
					axel->ready = 1;
					return;
				}
				if((axel->conn[j].increase_conn>=0)&&(axel->conn[j].currentbyte<axel->conn[j].lastbyte))
				{
					int new_index=axel->conn[j].increase_conn;
					if(new_index==MAX_NUM_CONNECTIONS)
					{
						conn_t * temp=axel->conn;
	 					 if(  axel->conn=realloc(axel->conn,  (  sizeof(conn_t)  *  (axel->conf->num_connections+1)  )   )    )
						  {
	  						new_index=axel->conf->num_connections;
	  	
	  						axel->conn[new_index].enabled=0;
	  	
	  						axel->conf->num_connections++;
	  	
	  					   }
	 					   else
	  				   	{
	  						axel->conn=temp;
	  						printf("\ndivide returining with no reallocation\n");
	  					}
	  	
	  					axel->conn[new_index].fd=-1;
						axel->conn[new_index].increase_conn=-1;
	
						axel->conn[new_index].enabled=0;
						
						axel->conn[new_index].lastbyte=-2;
						*axel->conn[new_index].setup_thread=0;
						axel->conn[new_index].index=new_index;
						axel->conn[new_index].conf = axel->conf;
						axel->conn[new_index].state=0;
						axel->conn[new_index].currentbyte=-1;
						
	
	
						if( !conn_set( &axel->conn[new_index], axel->url->text ) ) //parsing the url and initializing conn[0] . 
						{
							axel_message( axel, _("Could not parse URL.\n") );
							axel->ready = -1;
							printf("Could not parse URL.\n");
		
						}

						axel->conn[new_index].local_if = axel->conf->interfaces->text;
						axel->conf->interfaces = axel->conf->interfaces->next;
	  					
	  					
					}
					int index=j;
					if(axel->conn[new_index].fd>=0) 
						conn_disconnect(&axel->conn[new_index]);
					axel->conn[new_index].deliberate_close=-1;
					axel->conn[index].deliberate_close=-1;
					long long int remaining_size=(axel->conn[index].lastbyte - axel->conn[index].currentbyte) + 1;
					
					if(remaining_size<=200)
					{
						axel->conn[new_index].lastbyte=1;
						axel->conn[new_index].currentbyte=2;
						goto pq;
					}
	
					axel->conn[new_index].lastbyte=axel->conn[index].lastbyte;
	
					axel->conn[index].lastbyte=remaining_size/2 - 1 + axel->conn[index].currentbyte;

					axel->conn[new_index].currentbyte=axel->conn[index].lastbyte + 1;
					
					pq:printf("\nindex %d:current:%lld and last:%lld\nnew_index:%d current:%lld and last:%lld\n",index,axel->conn[index].currentbyte,axel->conn[index].lastbyte,new_index,axel->conn[new_index].currentbyte,axel->conn[new_index].lastbyte);
				 

	
					//axel->conn[index].deliberate_close=0;
					axel->conn[index].index=index;
					axel->conn[index].state=0;
					//axel->conn[index].conf = axel->conf;



					axel->conn[new_index].last_transfer=gettime();

					axel->conn[new_index].enabled=0;
					axel->conn[new_index].fd=-1;
					
					
					axel->conn[new_index].state=0;
					axel->conn[new_index].index=new_index;
					axel->conn[new_index].conf = axel->conf;
					*axel->conn[new_index].setup_thread=0;

					//axel->conn[new_index].conf = axel->conf;
					if( !conn_set( &axel->conn[new_index], axel->url->text ) ) //parsing the url and initializing conn[0] . 
					{
						axel_message( axel, _("Could not parse URL.\n") );
						axel->ready = -1;
						printf("Could not parse URL.\n");
		
					}
					
					axel->conn[new_index].local_if = axel->conf->interfaces->text;
					axel->conf->interfaces = axel->conf->interfaces->next;
					array_close[new_index]=0;
					axel->conn[j].increase_conn=-1;
					axel->conn[new_index].increase_conn=-1;
					axel->conn[new_index].increase_conn=-1;
					axel->conn[new_index].deliberate_close=0;
					axel->conn[index].deliberate_close=0;
					
				}
				axel->conn[j].fd=-1;
				axel->conn[j].enabled = 0;
			 	//to indicate that it has disconnected the connection.
				
				array_close[j]=0;
				//pthread_cond_signal(&sel_cond);
				
				printf("\nselect thread: closed conn:%d\n",j);
			}
		}
		
		FD_ZERO( fds );
		hifd = 0;
		
		for( i = 0; i < axel->conf->num_connections; i ++ )
		{
			if( axel->conn[i].fd>0 && (axel->conn[i].enabled))
				{FD_SET( axel->conn[i].fd, fds ); 
				//printf("**conn:%d  fd:%d\n",i,axel->conn[i].fd);
				}
			hifd = max( hifd, axel->conn[i].fd);
			//printf("hifd=%d\n",hifd);
	
		}
		
		if( hifd == 0 )
		{
			/* No connections yet. Wait...				*/
			int i=0;
			
			for(i=0;i<axel->conf->num_connections;i++)
printf("\nconn: %d , enabled:%d ,current: %d,last:%d  ,delib:%d, state: %d\n\n\n",i,axel->conn[i].enabled,axel->conn[i].currentbyte,axel->conn[i].lastbyte,axel->conn[i].deliberate_close,axel->conn[i].state);
			
			usleep( 100000 );
			continue;
		
			//goto conn_check;
		}
		else
		{
		
			timeval->tv_sec = 0;
			timeval->tv_usec = 100000;
			/* A select() error probably means it was interrupted
		   	by a signal, or that something else's very wrong...	*/
			int num_descriptors;
			if( (num_descriptors=select( hifd + 1, fds, NULL, NULL, timeval ) )== -1 )
			{
				printf("\n select returned -1\n");
				axel->ready = -1;
				return;
				
			}
			//else
			//printf("select() : ready %d\n",num_descriptors);
		}
	
		/* Handle connections which need attention			*/

		//if(FD_ISSET( axel->conn[2].fd, fds )) printf("\n yes for 3\n"); else printf("\n no for 3\n"); 
		
		for( i = 0; i < axel->conf->num_connections; i ++ )
		{
			if( axel->conn[i].fd>0 ) 
			{
				//printf("\n calling fd_isset %d\n",i);
				if( FD_ISSET( axel->conn[i].fd, fds ))
				{
		
					//printf("reading : connection %d\n",i);		
					axel->conn[i].last_transfer = gettime();
					size = read( axel->conn[i].fd, buffer, axel->conf->buffer_size );
					//printf("\n size is %d\n",size);
					if( size == -1 )
					{
						if( axel->conf->verbose )
						{
							axel_message( axel, _("Error on connection %i! "
							"Connection closed"), i );
						}
						axel->conn[i].enabled = 0;
						conn_disconnect( &axel->conn[i] );
						array_select[i]=-1;
						continue;
					}
					else if( size == 0 )
					{
						
						//Only abnormal behaviour if:		
						if( axel->conn[i].currentbyte < axel->conn[i].lastbyte && axel->size != INT_MAX )
						{
							axel_message( axel, _("Connection %i unexpectedly closed"), i );
						}
						else
						{
							axel_message( axel, _("Connection %i finished"), i );
							axel->num_tcp_conn --;
						}
						
						if( !axel->conn[0].supported )
						{
							printf("making axel->ready =1 :unsupported");
							axel->ready = 1;
						}
						axel->conn[i].enabled = 0;
						conn_disconnect( &axel->conn[i] );
						array_select[i]=-1;
						continue;
					}
					// remaining == Bytes to go					//
					remaining = axel->conn[i].lastbyte - axel->conn[i].currentbyte + 1;
					if( remaining < size )
					{
					
						axel_message( axel, _("Connection %i finished"), i );
						
						axel->conn[i].enabled = 0;
						axel->num_tcp_conn --;
						conn_disconnect( &axel->conn[i] );
						array_select[i]=-1;
						size = remaining;
						// Don't terminate, still stuff to write!	//
					}
					// This should always succeed..				//
					lseek( axel->outfd, axel->conn[i].currentbyte, SEEK_SET );
					if( write( axel->outfd, buffer, size ) != size )
					{
			
						axel_message( axel, _("Write error!") );
						axel->ready = -1;
						while(1) printf("write error");
					}
					axel->conn[i].currentbyte += size;
					axel->bytes_done += size;
				}
				else
				{
					if( gettime() > axel->conn[i].last_transfer + axel->conf->connection_timeout )
					{
						if( axel->conf->verbose )
						axel_message( axel, _("Connection %i timed out"), i );
						conn_disconnect( &axel->conn[i] );
						axel->conn[i].enabled = 0;
						array_select[i]=-1;
					}
				}
			}

		}
		

		/* Calculate current average speed and finish_time		*/
		axel->bytes_per_second = (int) ( (double) ( axel->bytes_done - axel->start_byte ) / ( gettime() - axel->start_time ) );
		axel->finish_time = (int) ( axel->start_time + (double) ( axel->size - axel->start_byte ) / axel->bytes_per_second );
		
		
		for(i=0;i<axel->conf->num_connections;i++)
		{
			if(axel->conn[i].enabled==1)
				live++;
			else if(axel->conn[i].currentbyte<axel->conn[i].lastbyte)
				idle++;
		}
		
		time_t timestamp=gettime();
		fprintf(data_fp,"%llu,%lld\n",(long long)timestamp,axel->bytes_done);
		
		
		
		/* Check speed. If too high, delay for some time to slow things
	   	down a bit. I think a 5% deviation should be acceptable.	*/
		if( axel->conf->max_speed > 0 )
		{
			if( (float) axel->bytes_per_second / axel->conf->max_speed > 1.05 )
				axel->delay_time += 10000;
			else if( ( (float) axel->bytes_per_second / axel->conf->max_speed < 0.95 ) && ( axel->delay_time >= 10000 ) )
				axel->delay_time -= 10000;
			else if( ( (float) axel->bytes_per_second / axel->conf->max_speed < 0.95 ) )
				axel->delay_time = 0;
			usleep( axel->delay_time );
		}
		//printf("bytes done %lld\n",axel->bytes_done);
		/*for(i=0;i<axel->conf->num_connections;i++)
		{
			fprintf(data_fp,"%d,%d,%d,%d",axel->conn[i].index,axel->conn[i].);
		}*/
		
		
		
		
		
		if( axel->bytes_done == axel->size )
		{
			printf("DEBUGGING: MAKING AXEL->READY=1");
			axel->ready = 1;
			return;
		}
	}
	
}

/* Add a message to the axel->message structure				*/
static void axel_message( axel_t *axel, char *format, ... )
{
	message_t *m = malloc( sizeof( message_t ) ), *n = axel->message;
	va_list params;
	
	memset( m, 0, sizeof( message_t ) );
	va_start( params, format );
	vsnprintf( m->text, MAX_STRING, format, params );
	va_end( params );
	
	if( axel->message == NULL )
	{
		axel->message = m;
	}
	else
	{
		while( n->next != NULL )
			n = n->next;
		n->next = m;
	}
}

/* Divide the file and set the locations for each connection		*/
static void axel_divide( axel_t *axel )
{
	int i;
	
	axel->conn[0].currentbyte = 0;
	axel->conn[0].lastbyte = axel->size / axel->conf->num_connections - 1;
	for( i = 1; i < axel->conf->num_connections; i ++ )
	{
#ifdef DEBUG
		printf( "Downloading %lld-%lld using conn. %i\n", axel->conn[i-1].currentbyte, axel->conn[i-1].lastbyte, i - 1 );
#endif
		axel->conn[i].currentbyte = axel->conn[i-1].lastbyte + 1;
		axel->conn[i].lastbyte = axel->conn[i].currentbyte + axel->size / axel->conf->num_connections;
	}
	axel->conn[axel->conf->num_connections-1].lastbyte = axel->size - 1;
#ifdef DEBUG
	printf( "Downloading %lld-%lld using conn. %i\n", axel->conn[i-1].currentbyte, axel->conn[i-1].lastbyte, i - 1 );
#endif
}

static void axel_divide_half(axel_t *axel,int index)
{
	int new_index;
	//save_state(axel);
	int l=-2;
	
	int found=0;
	int p=axel->conf->num_connections;
	while(p>=0)
	{
	  if((axel->conn[p].currentbyte>axel->conn[p].lastbyte)&&(axel->conn[p].currentbyte>=0)&&(axel->conn[p].lastbyte>=0))
	     {
		new_index=p;
		found=1;
		//conn_disconnect(&axel->conn[new_index]);
		axel->conn[new_index].enabled=0;
		printf("\n ** found %d\n",p);
		break;
	     }
	  p--;
	}

	if(axel->conn[index].state==1)
	{
		while(axel->conn[index].state!=0)
		{
			if( gettime() > axel->conn[index].last_transfer + axel->conf->reconnect_delay )
			{
				if ( *axel->conn[index].setup_thread != 0 )	
					pthread_cancel( *axel->conn[index].setup_thread );
				else
					{axel->conn[index].state = 0;break;}
				axel->conn[index].state = 0;
				break;
			}
		}
		if ( *axel->conn[index].setup_thread != 0 )
			l=pthread_join(*(axel->conn[index].setup_thread), NULL);
					
	}
	else
	{
		if ( *axel->conn[index].setup_thread != 0 )
			l=pthread_join(*(axel->conn[index].setup_thread), NULL);
			//printf("waiting to disconnect\n");
	}
	
	

	/*printf("divide: waiting for select to disconnect conn:%d\n",index);
	pthread_mutex_lock(&sel_mutex);
	while(array_close[index]!=2)	
	{
		//printf("waiting to disconnect conn %d\n",index);   //???ask sir about starvtion problem.
		pthread_cond_wait(&sel_cond,&sel_mutex);
	}
	array_close[index]=0;
	pthread_mutex_unlock(&sel_mutex);*/
	
	if(found!=1)
	{
	  
	  
          axel->conn[index].increase_conn=MAX_NUM_CONNECTIONS;
          axel->num_tcp_conn++;
          return;
	  //return ;
	}
	
	
	
	axel->conn[new_index].fd=-1;
	axel->conn[new_index].increase_conn=-1;
	
	axel->conn[new_index].enabled=0;
	axel->conn[index].increase_conn=new_index;
	axel->conn[new_index].lastbyte=-2;
	*axel->conn[new_index].setup_thread=0;
	axel->conn[new_index].index=new_index;
	axel->conn[new_index].conf = axel->conf;
	axel->conn[new_index].state=0;
	axel->conn[new_index].currentbyte=-1;
	array_close[index]=1;
	
	
	

	axel->conn[new_index].local_if = axel->conf->interfaces->text;
	axel->conf->interfaces = axel->conf->interfaces->next;
	
	//axel->conn[index].increase_conn=new_index;
	/*long long int remaining_size=(axel->conn[index].lastbyte - axel->conn[index].currentbyte) + 1;
	
	axel->conn[new_index].lastbyte=axel->conn[index].lastbyte;
	
	axel->conn[index].lastbyte=remaining_size/2 - 1 + axel->conn[index].currentbyte;

	axel->conn[new_index].currentbyte=axel->conn[index].lastbyte + 1;
	printf("\nindex %d:current:%lld and last:%lld\nnew_index:%d current:%lld and last:%lld\n",index,axel->conn[index].currentbyte,axel->conn[index].lastbyte,new_index,axel->conn[new_index].currentbyte,axel->conn[new_index].lastbyte);
 

	
	axel->conn[index].deliberate_close=0;
	axel->conn[index].index=index;
	axel->conn[index].state=0;
	//axel->conn[index].conf = axel->conf;



	axel->conn[new_index].last_transfer=gettime();

	axel->conn[new_index].enabled=0;
	axel->conn[new_index].deliberate_close=0;
	axel->conn[new_index].increase_conn=-1;
	axel->conn[new_index].state=0;
	axel->conn[new_index].index=new_index;
	axel->conn[new_index].conf = axel->conf;
	*axel->conn[new_index].setup_thread=0;

	//axel->conn[new_index].conf = axel->conf;
	if( !conn_set( &axel->conn[new_index], axel->url->text ) ) //parsing the url and initializing conn[0] . 
	{
		axel_message( axel, _("Could not parse URL.\n") );
		axel->ready = -1;
		printf("Could not parse URL.\n");
		
	}

	axel->conn[new_index].local_if = axel->conf->interfaces->text;
	axel->conf->interfaces = axel->conf->interfaces->next;
	*/

	
        
	axel->num_tcp_conn++;
	
	printf("\n exiting divide successfully\n");
	return;

}

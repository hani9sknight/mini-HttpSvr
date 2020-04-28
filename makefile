server: main.cc ./threadpool/thread_pool.h ./http/http_connection.cc ./http/http_connection.h ./semaphore/semaphore.h ./logger/logger.cc ./logger/logger.h ./logger/block_queue.h ./cgi/mysql_connect_pool.cc ./cgi/mysql_connect_pool.h
	g++ -o server main.cc ./threadpool/thread_pool.h ./http/http_connection.h ./http/http_connection.cc ./semaphore/semaphore.h ./logger/logger.cc ./logger/logger.h ./cgi/mysql_connect_pool.cc -lpthread -lmysqlclient -I . -O2

CGISQL.cgi: ./cgi/sign.cc ./cgi/mysql_connect_pool.cc ./cgi/mysql_connect_pool.h
	g++ -o ./root/CGISQL.cgi ./cgi/sign.cc ./cgi/mysql_connect_pool.cc ./cgi/mysql_connect_pool.h -lmysqlclient -I . -O2

clean:
	rm -r server
	rm -r ./root/CGISQL.cgi
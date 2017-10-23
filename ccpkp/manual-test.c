#include<stdio.h>
#include<stdlib.h>
#include<errno.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>
#include <stdint.h>	/* for uint64 definition */
#include <time.h>	/* for clock_gettime */

#define BILLION 1000000000L
 
#define BUFFER_LENGTH 256               ///< The buffer length (crude but fine)
static char receive[BUFFER_LENGTH];     ///< The receive buffer from the LKM
 
int main(){
   int ret, fd;
   char stringToSend[BUFFER_LENGTH];
   uint64_t diff;
   struct timespec start, end;

   fd = open("/dev/ccpkp", O_RDWR);             // Open the device with read/write access
   if (fd < 0){
      perror("failed to open the device...");
      return errno;
   }
	 printf("enter message, hit enter to read, or quit\n");
	 while (1) {
		 printf("> ");
		 ret = scanf("%[^\n]%*c", stringToSend);                // Read in a string (with spaces)

		 if (strcmp(stringToSend, "quit") == 0) {
				break;
		 } else if (strcmp(stringToSend, "read") == 0) {
			 clock_gettime(CLOCK_MONOTONIC, &start);
			 ret = read(fd, receive, BUFFER_LENGTH);        // Read the response from the LKM
			 clock_gettime(CLOCK_MONOTONIC, &end);
			 if (ret < 0){
					perror("Failed to read the message from the device.");
					return errno;
			 }
			 receive[ret] = '\0';
			 printf("%s\n", receive);
		 } else {
			 clock_gettime(CLOCK_MONOTONIC, &start);
			 ret = write(fd, stringToSend, strlen(stringToSend)); // Send the string to the LKM
			 clock_gettime(CLOCK_MONOTONIC, &end);
			 if (ret < 0){
					perror("failed to write the message to the device.");
					return errno;
			 }
		 }
		 diff = BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
		 printf("elapsed = %llu us\n", ((long long unsigned int) diff)/1000);
	 }
   return 0;
}

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "dasio.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{
	int data;
	int i;
	int option;
	int *buffer;
	int s;
	int length;
	int wait;
	int run = 1;
	char file_name[50];
	FILE * outfd;
	int n, j;
	int dasfd;

	/*Initialize " Globals "*/
	dasfd = open("/ dev / das0 ", O_RDWR, 0);
	if (dasfd < 0)
	{
		fprintf(stderr, " osc : could not open / dev / das0 \ n ");
		perror(" osc ");
		return 1;
	}

	while (run)
	{
		printf("\ n\ n1 . Set Channel \n ");
		printf("2. Get Channel \n ");
		printf("3. Set Rate \n ");
		printf("4. Get Rate \n ");
		printf("5. Start Sampling \ n ");
		printf("6. Stop Sampling \n ");
		printf("7. Read from Device \n ");
		printf("8. Osciliscope (DANGER)\n ");
		printf("9. Write Data to File \n ");
		printf("10. Exit \n ");
		printf("11. Flip bit in Control Register \n ");
		printf("12. Get Status Register \n ");
		printf("13. Blinky Lights \n ");
		printf(" Select Option : ");

		s = scanf("% d", &option);
		if (s < 1)
		{
			option = 15;
			while (fgetc(stdin) != ’\n’ && !feof(stdin)) /*spin ! */;
		}

		switch (option)
		{
			case 1:
				printf(" Enter a channel[0 -7]: ");
				scanf("% d ", &data);
				if (ioctl(dasfd, DAS_SET_CHANNEL, &data) != 0)
					perror(" Set Channel : ");
				break;

			case 2:
				data = -1;
				if (ioctl(dasfd, DAS_GET_CHANNEL, &data) == 0)
					printf("\ nChannel = 0x% x\n", data);
				else
					perror(" Get Channel : ");
				break;

			case 3:
				printf(" Select a rate : ");
				scanf("% d ", &data);
				if (ioctl(dasfd, DAS_SET_RATE, &data) != 0)
					perror(" Set Rate : ");
				break;

			case 4:
				data = -1;
				if (ioctl(dasfd, DAS_GET_RATE, &data) == 0)
					printf("\ nRATE = %d \n", data);
				else
					perror(" Get Rate : ");
				break;

			case 5:
				if (ioctl(dasfd, DAS_START_SAMPLING, NULL) != 0)
					perror(" Start Sampling : ");
				break;

			case 6:
				if (ioctl(dasfd, DAS_STOP_SAMPLING, NULL) != 0)
					perror(" Stop Sampling : ");
				break;

			case 7:
				printf(" How many data Points[0 -1000]? ");
				scanf("% d ", &data);
				buffer = (int*) malloc(sizeof(int) *data);
				n = read(dasfd, buffer, (data* sizeof(int)));
				n /= sizeof(int);
				for (i = 0; i < n; i++)
					printf(" Time : % d Value :0 x% x\n", buffer[i] > > 16, buffer[i] &0 xffff);
				printf(" Actually read % d\n", n);
				free(buffer);
				break;

			case 8:
				printf(" You will HAVE to press CTRL -C to exit this mode !\ n ");
				printf(" How many data points should be retrieved at a time ? ");
				scanf("% d ", &data);
				s = data;
				while (1)
				{
					buffer = (int*) malloc(sizeof(int) *s);
					n = read(dasfd, buffer, (s* sizeof(int)));
					n /= (sizeof(int));
					for (data = 0; data < n; data++)
					{
						length = ((buffer[data] &0 xfff) *80) / 0 xfff;
						for (j = 0; j < length; j++)
						{
							printf(" -");
						}

						printf("*\ n ");
						wait = 0;
						while (wait < 1000)
							wait++;
					}

					free(buffer);
				}

				break;

			case 9:
				printf(" How many data points would you like ? ");
				scanf("% d ", &data);
				printf(" What file would you like to write to ? ");
				scanf("% s ", file_name);
				outfd = NULL;
				outfd = fopen(file_name, "w +");
				if (NULL == outfd)
				{
					fprintf(stderr, " osc : could not open %s\n", file_name);
					exit(1);
				}

				n = read(dasfd, buffer, data *(sizeof(int)));
				for (i = 0; i < (n / sizeof(int)); i++)
				{
					fprintf(outfd, "[% d :0 x%x ]\ n", buffer[i] >> 16, buffer[i] &0 xffff);
				}

				fprintf(outfd, " Actually Read % d data Sets \n", n / sizeof(int));
				fclose(outfd);
				break;

			case 10:
				run = 0;
				break;

			case 11:
				printf(" Flip a bit in the control register \n ");
				data = -1;
				if (ioctl(dasfd, DAS_GET_REGISTER, &data) != 0)
				{
					perror(" Get Register : ");
					break;
				}

				printf(" OP4 (7) (disabled)\n ");
				printf(" OP3 (6) \n ");
				printf(" OP2 (5) \n ");
				printf(" OP1 (4) \n ");
				printf(" INTE (3) \ n ");
				printf(" Chan (0 -2) : % d\n", data &(0 x7));
				printf(" which bit ?: ");
				scanf("% d ", &data);
				ioctl(dasfd, DAS_SET_REGISTER, &data);
				break;

			case 12:
				printf(" Receiving Status register info \n ");
				data = -1;
				if (ioctl(dasfd, DAS_GET_REGISTER, &data) != 0)
				{
					perror(" Get Register : ");
					break;
				}

				printf(" EOC : %d\n ", (data &(1<< 7)) > > 7);
				printf(" IP3 : %d\n ", (data &(1<< 6)) > > 6);
				printf(" IP2 : %d\n ", (data &(1<< 5)) > > 5);
				printf(" IP1 : %d\n ", (data &(1<< 4)) > > 4);
				printf(" IRQ : %d\n ", (data &(1<< 3)) > > 3);
				printf(" Chan : % d\n", data &(0 x7));
				break;

			case 13:
				printf(" Going to count up the seconds (0 -7) with a digital clock \n ");
				printf(" This cycles through OP1 - OP3 in reverse order (1= OP3) \n ");
				data = -1;
				ioctl(dasfd, DAS_SET_REGISTER, &data);
				for (n = 0; n < 8; n++)
				{
					switch (n)
					{
						case 0:
							data = -1;
							ioctl(dasfd, DAS_SET_REGISTER, &data);
							break;
						case 1:
							data = 6;
							ioctl(dasfd, DAS_SET_REGISTER, &data);
							break;
						case 2:
							data = 6;
							ioctl(dasfd, DAS_SET_REGISTER, &data);
							data = 5;
							ioctl(dasfd, DAS_SET_REGISTER, &data);
							break;
						case 3:
							data = 6;
							ioctl(dasfd, DAS_SET_REGISTER, &data);
							break;
						case 4:
							data = 6;
							ioctl(dasfd, DAS_SET_REGISTER, &data);
							data = 5;
							ioctl(dasfd, DAS_SET_REGISTER, &data);
							data = 4;
							ioctl(dasfd, DAS_SET_REGISTER, &data);
							break;
						case 5:
							data = 6;
							ioctl(dasfd, DAS_SET_REGISTER, &data);
							break;
						case 6:
							data = 6;
							ioctl(dasfd, DAS_SET_REGISTER, &data);
							data = 5;
							ioctl(dasfd, DAS_SET_REGISTER, &data);
							break;
						case 7:
							data = 6;
							ioctl(dasfd, DAS_SET_REGISTER, &data);
							break;
						default:
							break;
					}

					sleep(1);
				}

				break;
			default:
				printf(" Not an option \n ");
		}
	}

	exit(0);
	return 0;
}
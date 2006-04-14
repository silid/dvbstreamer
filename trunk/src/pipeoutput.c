#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <fcntl.h>

#include "ts.h"
#include "main.h"

struct PipeOutputState
{
	char pipe[PATH_MAX];
	int fd;
};

void *PipeOutputCreate(char * arg)
{
	struct PipeOutputState *state = malloc(sizeof(struct PipeOutputState));
	if (state == NULL)
	{
		return NULL;
	}
	printlog(1, "Creating pipe %s\n", arg);
	strcpy(state->pipe, arg);
	/*if (mkfifo(state->pipe, S_IRUSR | S_IWUSR) < 0)
	{
		free(state);
		return NULL;
	}*/
	printlog(1, "Pipe created\n");
	state->fd = open(state->pipe, O_RDONLY);
	if (state->fd < 0)
	{
		unlink(state->pipe);
		free(state);
		return NULL;
	}
	printlog(1, "Pipe opened\n");
	return state;
}

void PipeOutputClose(void *arg)
{
	struct PipeOutputState *state = arg;
	close(state->fd);
	unlink(state->pipe);
	free(state);
}

void PipeOutputPacketOutput(void *arg, int numberOfPackets, TSPacket_t *packets)
{
	struct PipeOutputState *state = arg;
	// Poll pipe fd to see if we can output 
	struct pollfd fds[1];
	fds[0].fd = state->fd;
	fds[0].events = POLLOUT;
	
	if (poll(fds, 1, 0) == 1)
	{
		if (fds[0].revents & POLLOUT)
		{
			write(state->fd, packets, sizeof(TSPacket_t) * numberOfPackets);
		}
	}
}

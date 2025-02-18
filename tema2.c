#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define MAX_PEERS 100

// requests owners of a file
#define MSG_GET_OWNERS "GET_OWNERS"
// requests a segment of a file
#define MSG_GET_SEGMENT "GET_SEGMENT"

typedef struct {
    char name[MAX_FILENAME];
    int num_segments;
    // stores the hash of each segment
    char segments[MAX_CHUNKS][HASH_SIZE + 1];
} file_t;

typedef struct {
    // files owned by the client
    file_t files[MAX_FILES];
    int num_owned_files;
    // number of files that the client wants to download
    int num_desired_files;
    // files wanted by the client
    char desired_files[MAX_FILES][MAX_FILENAME];
} client_t;

typedef struct {
    char file_name[MAX_FILENAME];
    // number of segments that the file is split into
    int num_segments;
    int clients_with_segments[MAX_PEERS];
    // number of clients that have segments of the file
    int num_clients;
} swarm_t;

typedef struct {
    int rank;
    client_t client;
    // mutex for accessing the client structure
    pthread_mutex_t mutex;
} peer_info_t;

// stores information about different swarms
swarm_t swarms[MAX_FILES];
int swarm_count = 0;
// stores information about the current peer
peer_info_t peer_info;

// reads the input file corresponding to the peer's rank and initializes the client's owned and desired files
void read_input_files(int rank, client_t *client) {
    char file_name[20];
    sprintf(file_name, "in%d.txt", rank);
    FILE *fp = fopen(file_name, "r");

    if (!fp) {
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // reads the number of files that the client owns
    fscanf(fp, "%d", &(client->num_owned_files));
    // reads the details of each file owned by the client
    for (int i = 0; i < client->num_owned_files; i++) {
        fscanf(fp, "%s %d", client->files[i].name, &(client->files[i].num_segments));
        // iterates through the segments of the file to read their hashes
        for (int j = 0; j < client->files[i].num_segments; j++) {
            fscanf(fp, "%s", client->files[i].segments[j]);
        }
    }

    // reads the number of files that the client desires
    fscanf(fp, "%d", &(client->num_desired_files));
    for (int i = 0; i < client->num_desired_files; i++) {
        fscanf(fp, "%s", client->desired_files[i]);
    }

    fclose(fp);
}

//handles the download of the desired files
void *download_thread_func(void *arg)
{
    int rank = *(int*) arg;

    // as long as the client has desired files to download, it iterates through them
    while(peer_info.client.num_desired_files > 0) {
        for(int i = 0; i < peer_info.client.num_desired_files; i++) {
            char filename[MAX_FILENAME];
            strcpy(filename, peer_info.client.desired_files[i]);
            // prepares a request message to get the owners
            char owners_request[] = MSG_GET_OWNERS;

            // sends the message to the tracker and it associates it tag 2
            MPI_Send(owners_request, strlen(owners_request) + 1, MPI_CHAR, TRACKER_RANK, 2, MPI_COMM_WORLD);
            // sends the filename to the tracker
            MPI_Send(filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 2, MPI_COMM_WORLD);

            swarm_t swarm;
            memset(&swarm, 0, sizeof(swarm_t));

            // receives the number of segments from the tracker with tag 3 associated
            MPI_Recv(&swarm.num_segments, 1, MPI_INT, TRACKER_RANK, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // receives the number of clients that have the file
            MPI_Recv(&swarm.num_clients, 1, MPI_INT, TRACKER_RANK, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            for(int j = 0; j < swarm.num_clients; j++) {
                MPI_Recv(&swarm.clients_with_segments[j], 1, MPI_INT, TRACKER_RANK, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }

            char segments[MAX_CHUNKS][HASH_SIZE + 1];
            memset(segments, 0, sizeof(segments));

            // iterates over each segment to download
            for(int j = 0; j < swarm.num_segments; j++) {
                int chosen_peer = -1;

                // iterates over each client that has the segments
                for(int k = 0; k < swarm.num_clients; k++) {
                    // selects a peer
                    chosen_peer = swarm.clients_with_segments[k];

                    if (chosen_peer == -1) continue;

                    char segment_request[] = MSG_GET_SEGMENT;
                    // sends the message to get segment to the chosen peer with tag 5 associated
                    MPI_Send(segment_request, strlen(segment_request) + 1, MPI_CHAR, chosen_peer, 5, MPI_COMM_WORLD);
                    // sends the filename to the chosen peer
                    MPI_Send(filename, MAX_FILENAME, MPI_CHAR, chosen_peer, 5, MPI_COMM_WORLD);
                    // sends the segment index to the chosen peer
                    MPI_Send(&j, 1, MPI_INT, chosen_peer, 5, MPI_COMM_WORLD);
                    // receives the segment from the chosen peer with tag 15 associated
                    MPI_Recv(segments[j], HASH_SIZE + 1, MPI_CHAR, chosen_peer, 15, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    
                    if(strlen(segments[j]) > 0) {
                        break;
                    }
                }

                if (chosen_peer == -1 || strlen(segments[j]) == 0) {
                    continue;
                }
            }

            pthread_mutex_lock(&peer_info.mutex);

            // checks if the client has enough space to store the downloaded file
            if (peer_info.client.num_owned_files < MAX_FILES) {
                strcpy(peer_info.client.files[peer_info.client.num_owned_files].name, filename);
                peer_info.client.files[peer_info.client.num_owned_files].num_segments = swarm.num_segments;

                for (int k = 0; k < swarm.num_segments; k++) {
                    strcpy(peer_info.client.files[peer_info.client.num_owned_files].segments[k], segments[k]);
                }

                peer_info.client.num_owned_files++;
            }

            pthread_mutex_unlock(&peer_info.mutex);
            char output_file_name[100];
            sprintf(output_file_name, "client%d_%s", rank, filename);
            FILE *fp = fopen(output_file_name, "w");

            if (!fp) {
                MPI_Abort(MPI_COMM_WORLD, 1);
            }

            for(int k = 0; k < swarm.num_segments; k++) {
                fprintf(fp, "%s\n", segments[k]);
            }

            fclose(fp);
        }

        peer_info.client.num_desired_files = 0;
    }

    // sends a ready message to the tracker that indicates the completion of downloads
    char ready[] = "READY";
    MPI_Send(ready, strlen(ready) + 1, MPI_CHAR, TRACKER_RANK, 2, MPI_COMM_WORLD);
}

// handles upload requests
void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;

    while (1) {
        MPI_Status status;
        // initializes a buffer that stores the incoming request message
        char request[15] = "";

        // receives a message from any source with tag 5 associated
        MPI_Recv(request, sizeof(request), MPI_CHAR, MPI_ANY_SOURCE, 5, MPI_COMM_WORLD, &status);
        
        // if the message received is of type GET_SEGMENT
        if (strcmp(request, MSG_GET_SEGMENT) == 0) {
            char filename[MAX_FILENAME];
            int segment_index = -1;
            
            // receives the filename from the requesting peer
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 5, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // receives the segment index
            MPI_Recv(&segment_index, 1, MPI_INT, status.MPI_SOURCE, 5, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            // iterates over the owned fles in order to find the requested file
            for(int i = 0; i < peer_info.client.num_owned_files; i++) {
                if (strcmp(peer_info.client.files[i].name, filename) == 0) {
                    // checks if the segment index is valid
                    if(segment_index >= 0 && segment_index < peer_info.client.files[i].num_segments) {
                        // sends the requested segment hash to the requester with tag 15 associated
                        MPI_Send(peer_info.client.files[i].segments[segment_index], HASH_SIZE + 1, MPI_CHAR, status.MPI_SOURCE, 15, MPI_COMM_WORLD);
                    } else {
                        char empty_hash[HASH_SIZE + 1] = "";
                        // sends an empty hash that indicates an invalid request
                        MPI_Send(empty_hash, HASH_SIZE + 1, MPI_CHAR, status.MPI_SOURCE, 15, MPI_COMM_WORLD);
                    }
                    break;
                }
            }
        } else if (strcmp(request, "DONE") == 0) {
            return NULL;
        }
    }
}

// manages information about files and their owners, and coordinates the peers
void tracker(int numtasks, int rank) {
    // stores the number of files owned and the number of segments per file
    int files_owned, segments;
    char filename[MAX_FILENAME];

    // iterates over all peers
    for(int i = 1; i < numtasks; i++) {
        // receives the number of files owned by the current peer with tag 0 associated
        MPI_Recv(&files_owned, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        // iterates over the files owned by the current peer
        for(int j = 0; j < files_owned; j++) {
            // receives the filename
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // receives the number of segments for the file
            MPI_Recv(&segments, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            int existing = -1;
            
            // checks if the file already exists in the swarms array
            for(int k = 0; k < swarm_count; k++) {
                // if the file is found, it stores its index
                if (strcmp(swarms[k].file_name, filename) == 0) {
                    existing = k;
                    break;
                }
            }
            
            // if it doesn't exist, it adds it to the swarms array
            if (existing == -1) {
                strcpy(swarms[swarm_count].file_name, filename);
                swarms[swarm_count].num_segments = segments;
                swarms[swarm_count].num_clients = 0;
                swarm_count++;
                existing = swarm_count -1;
            }
            
            int already_present = 0;
            
            // checks if the current peer is already in the list of clients that have the file
            for(int c = 0; c < swarms[existing].num_clients; c++) {
                if (swarms[existing].clients_with_segments[c] == i) {
                    already_present = 1;
                    break;
                }
            }
            
            // if the peer is not in the list, it adds it
            if (!already_present) {
                swarms[existing].clients_with_segments[swarms[existing].num_clients++] = i;
            }
            
            // receives the hash of each segment from peer i
            for(int s = 0; s < segments; s++) {
                char hash[HASH_SIZE + 1];
                MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
        }
    }
    
    // indicates successful receipt of file information
    for(int i = 1; i < numtasks; i++) {
        char response[] = "FILES_OK";
        MPI_Send(response, strlen(response) + 1, MPI_CHAR, i, 0, MPI_COMM_WORLD);
    }
    
    int clients_ready[numtasks];
    memset(clients_ready, 0, sizeof(clients_ready));
    
    while(1) {
        char request[15];
        MPI_Status status;
        
        MPI_Recv(request, sizeof(request), MPI_CHAR, MPI_ANY_SOURCE, 2, MPI_COMM_WORLD, &status);
        
        // if it receives a request of type GET_OWNERS
        if (strcmp(request, MSG_GET_OWNERS) == 0) {
            char filename_req[MAX_FILENAME];
            // receives the requested filename
            MPI_Recv(filename_req, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            int swarm_index = -1;
            
            // searches for the file in the swarms array
            for(int i = 0; i < swarm_count; i++) {
                // if the file is found, it stores the swarm index
                if (strcmp(swarms[i].file_name, filename_req) == 0) {
                    swarm_index = i;
                    break;
                }
            }
            
            // if the swarm exists, it sends the number of segments and the number of clients that have the file
            if (swarm_index != -1) {
                MPI_Send(&swarms[swarm_index].num_segments, 1, MPI_INT, status.MPI_SOURCE, 3, MPI_COMM_WORLD);
                MPI_Send(&swarms[swarm_index].num_clients, 1, MPI_INT, status.MPI_SOURCE, 3, MPI_COMM_WORLD);
                
                // sends each client rank that has the file
                for(int j = 0; j < swarms[swarm_index].num_clients; j++) {
                    MPI_Send(&swarms[swarm_index].clients_with_segments[j], 1, MPI_INT, status.MPI_SOURCE, 3, MPI_COMM_WORLD);
                }
            } else {
                int zero = 0;
                MPI_Send(&zero, 1, MPI_INT, status.MPI_SOURCE, 3, MPI_COMM_WORLD);
                MPI_Send(&zero, 1, MPI_INT, status.MPI_SOURCE, 3, MPI_COMM_WORLD);
            }
        }
        
        if (strcmp(request, "READY") == 0) {
            clients_ready[status.MPI_SOURCE] = 1;
            int everyone_ready = 1;
            
            // checks if all the clients are ready
            for (int i = 1; i < numtasks; i++) {
                if (!clients_ready[i]) {
                    everyone_ready = 0;
                    break;
                }
            }
            
            // if all the clients are ready, it sends a done message
            if (everyone_ready) {
                char done[] = "DONE";
                
                for (int i = 1; i < numtasks; i++) {
                    MPI_Send(done, strlen(done) + 1, MPI_CHAR, i, 5, MPI_COMM_WORLD);
                }
                
                return;
            }
        }
    }
}

// hadles the behavior of the peer
void peer(int numtasks, int rank) {
    peer_info.rank = rank;
    peer_info.client.num_owned_files = 0;
    peer_info.client.num_desired_files = 0;
    
    memset(peer_info.client.desired_files, 0, sizeof(peer_info.client.desired_files));
    pthread_mutex_init(&peer_info.mutex, NULL);
    read_input_files(rank, &peer_info.client);
    
    // sends the number of owned files to the tracker with tag 0 associated
    MPI_Send(&peer_info.client.num_owned_files, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
    
    // iterates over each owned file
    for(int i = 0; i < peer_info.client.num_owned_files; i++) {
        // sends the filename and the number of segments
        MPI_Send(peer_info.client.files[i].name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(&peer_info.client.files[i].num_segments, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
        
        for(int j = 0; j < peer_info.client.files[i].num_segments; j++) {
            // sends the segment hash
            MPI_Send(peer_info.client.files[i].segments[j], HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        }
    }
    
    char response[10];
    MPI_Recv(response, sizeof(response), MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    
    if (strcmp(response, "FILES_OK") != 0) {
        exit(-1);
    }

    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }

    pthread_mutex_destroy(&peer_info.mutex);
}

int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}


#include <math.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <err.h>
#include <mpi.h>
#include "photon.h"

#define PHOTON_BUF_SIZE (1024*1024*1024) // 1024M
#define PHOTON_TAG       UINT32_MAX

#define LIST_LIMIT      1000
#define ITER            20


using namespace std;

struct photon_config_t cfg;

bool verbose(false);

typedef struct parcel {
    int source;
    int rank_list[LIST_LIMIT];
  } parcel;

static void *wait_local(int &send_comp) {
  photon_cid request;
  int flag, rc, src;

  while (send_comp) {
    rc = photon_probe_completion(PHOTON_ANY_SOURCE, &flag, NULL, &request, &src, NULL,
                                 PHOTON_PROBE_EVQ);
    if (rc != PHOTON_OK)
      continue;  // no events                                                                                                                             
    if (flag && (request.u64 == PHOTON_TAG)) {
      send_comp--;
//      if (verbose) printf("%d send remains\n", send_comp);
    }
  }
  return NULL;
}

int send_done(int n, int r) {
  int i, send_comp(0);
  photon_cid lid, rid;
  lid.u64 = PHOTON_TAG;
  lid.size = 0;
  rid.u64 = 0xdeadbeef;
  rid.size =0;
  for (i=0; i<n; i++) {
    if (i==r)
      continue;
    photon_put_with_completion(i, 0, NULL, NULL, lid, rid, 0);
    send_comp++;
  }
	wait_local(send_comp);
  return 0;
}

//unsigned long upper_power_of_two(unsigned long v)
//{
//    v--;
//    v |= v >> 1;
//    v |= v >> 2;
//    v |= v >> 4;
//    v |= v >> 8;
//    v |= v >> 16;
//    v++;
//    return v;
//}

int get_target(int root, int rank, int* rank_list, int &rank_list_size, int* target_list, int &target_list_size)
{
	int dst;
	int myrank = -1;
	int root_rank = -1;
	int i;
	for (int i=0; i<rank_list_size && (myrank==-1 || root_rank==-1); i++)
	{
		if (rank_list[i] == rank)
		{
		  myrank = i;
		}
		if (rank_list[i] == root)
		{
		  root_rank = i;
		}
	}
	if(myrank==-1) exit(-1);
	if(root_rank==-1) exit(-1);
  int relative_rank = (myrank >= root_rank) ? myrank - root_rank : myrank - root_rank + rank_list_size;
//  int n = upper_power_of_two(rank_list_size);
  target_list_size=0;
	int mask = 0x1;
	while (mask < rank_list_size)
	{
		if (relative_rank & mask)
		{
		  break;
		}
		mask <<= 1;
	}
	mask >>= 1;
	while (mask > 0)
	{
		if (relative_rank + mask < rank_list_size)
		{
	    dst = myrank + mask;
	    if (dst >= rank_list_size) dst -= rank_list_size;
      target_list[target_list_size] = rank_list[dst];
      target_list_size++;
		}
		mask >>= 1;
	}
  return 0;
}

int receive_once(int rank, int nproc, int i, char* send, char **recv) {
  photon_cid request;
  int flag, src;
  do {
    photon_probe_completion(PHOTON_ANY_SOURCE, &flag, NULL, &request, &src, NULL, PHOTON_PROBE_LEDGER);
    if (flag && (request.u64 == 0xcafebabe)) {
      if (verbose) printf("%d received parcel from %d\n", rank, src);
      //send=recv[src];
      //memcpy(send, recv[src], PHOTON_BUF_SIZE*sizeof(uint8_t));
      send=recv[0];
      //memcpy(send, recv[0], i);
  		return 0;
    }
  } while (1);
}

int transfer(int rank, int nproc, int i, char* send, char **recv, struct photon_buffer_t lbuf, struct photon_buffer_t* rbuf, photon_cid lid, photon_cid rid) {
  photon_cid request;
  int flag, src, send_comp(0);
  do {
    photon_probe_completion(PHOTON_ANY_SOURCE, &flag, NULL, &request, &src, NULL, PHOTON_PROBE_LEDGER);
    if (flag && (request.u64 == 0xcafebabe)) {
      if (verbose) printf("%d received parcel from %d\n", rank, src);
      //send=recv[src];
      //memcpy(send, recv[src], PHOTON_BUF_SIZE*sizeof(uint8_t));
      send=recv[0];
      //memcpy(send, recv[0], i);
      parcel* pack = reinterpret_cast<parcel*> (send);
      int* send_list;
      int send_list_size;
      send_list = (int *)malloc(nproc * sizeof(int));
      get_target(pack->source, rank, pack->rank_list, nproc, send_list, send_list_size);
      lbuf.addr = (uintptr_t)send;
      lbuf.size = i;
      lbuf.priv = (struct photon_buffer_priv_t){0,0};
      for (int j=0; j<send_list_size; j++) {
        photon_put_with_completion(send_list[j], i, &lbuf, &rbuf[send_list[j]], lid, rid, 0);
        send_comp++;
	  		if (verbose) printf("%d send parcel to %d\n", rank, send_list[j]);
      }
    }
  } while (request.u64 != 0xdeadbeef);
	wait_local(send_comp);

  return 0;
}

int main(int argc, char *argv[]) {
  
  float a;
  int i, j, k, ns, send_comp(0);
  int rank, nproc;
  
  //configuration
	cfg.nproc = 0;
	cfg.address = 0;
	cfg.forwarder.use_forwarder = 0;
	cfg.ibv.use_cma = 0;
	cfg.ibv.use_ud  = 0;
	cfg.ibv.num_srq = 0;
	cfg.ibv.eth_dev = "roce0";
	cfg.ibv.ib_dev  = "mlx4_0+mlx5_0+qib0+hfi1_0";
	cfg.ibv.ud_gid_prefix = "ff0e::ffff:0000:0000";
	cfg.ugni.eth_dev = NULL;
	cfg.ugni.bte_thresh = -1;
	cfg.fi.provider = "sockets";
	cfg.cap.max_cid_size   = -1;
	cfg.cap.small_msg_size = -1;
	cfg.cap.small_pwc_size = -1;
	cfg.cap.eager_buf_size = -1;
	cfg.cap.pwc_buf_size   = -1;
	cfg.cap.ledger_entries = -1;
	cfg.cap.max_rd         = -1;
	cfg.cap.default_rd     = -1;
	cfg.cap.num_cq         = -1;
	cfg.cap.use_rcq        =  1;
	cfg.attr.comp_order     = PHOTON_ORDER_DEFAULT;
	cfg.exch.allgather = NULL;
	cfg.exch.barrier = NULL;
	cfg.meta_exch = PHOTON_EXCH_MPI;
	cfg.comm = NULL;
	cfg.backend = PHOTON_BACKEND_DEFAULT;
	cfg.coll = PHOTON_COLL_IFACE_PWC;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nproc);

  cfg.nproc = nproc;
  cfg.address = rank;
  
  
  if (argc > 1) {
    verbose = true;
    if (!rank)
      printf("verbose mode\n");
  }

  if (photon_init(&cfg) != PHOTON_OK) {
    exit(1);
  }
  		
// DEBUG
//  if (rank==0)
//    for(int i=0; i<nproc; i++) {
//			for(int j=0; j<nproc; j++) {
//			  vector<int> send_list = get_target(i, j, rank_list);
//			  printf("Root: %d\tRank: %d\tSend to:\n", i, j);
//			  for(int k=0; k<send_list.size(); k++) {
//			    printf("%d\t", send_list[k]);
//			  }
//			  printf("\n");
//			}
//    }

  struct photon_buffer_t lbuf;
  struct photon_buffer_t rbuf[nproc];
  photon_rid recvReq[nproc], sendReq[nproc];
  photon_cid lid, rid;
  char *send = NULL, *recv[nproc];

  lid.u64 = PHOTON_TAG;
  lid.size = 0;
  rid.u64 = 0xcafebabe;
  rid.size = 0;

  // only need one send buffer
  posix_memalign((void **) &send, 8, PHOTON_BUF_SIZE*sizeof(uint8_t));
  photon_register_buffer(send, PHOTON_BUF_SIZE);

  // ... but recv buffers for each potential sender
  //for (i=0; i<nproc; i++) {
  for (i=0; i<1; i++) {
    posix_memalign((void **) &recv[i], 8, PHOTON_BUF_SIZE*sizeof(uint8_t));
    photon_register_buffer(recv[i], PHOTON_BUF_SIZE);
  }
  
  for (i=0; i<nproc; i++) {
    // everyone posts their recv buffers
    //photon_post_recv_buffer_rdma(i, recv[i], PHOTON_BUF_SIZE, PHOTON_TAG, &recvReq[i]);
    photon_post_recv_buffer_rdma(i, recv[0], PHOTON_BUF_SIZE, PHOTON_TAG, &recvReq[i]);
  }

  for (i=0; i<nproc; i++) {
    // wait for a recv buffer that was posted
    photon_wait_recv_buffer_rdma(i, PHOTON_ANY_SIZE, PHOTON_TAG, &sendReq[i]);
    // get the remote buffer info so we can do our own put
    photon_get_buffer_remote(sendReq[i], &rbuf[i]);
    photon_send_FIN(sendReq[i], i, PHOTON_REQ_COMPLETED);
    photon_wait(recvReq[i]);
  }  

  // now we can proceed with our benchmark
  if (rank == 0)
    printf("%-7s%-9s%-9s%-12s%-12s%-14s\n", "Ranks", "Senders", "Method", "Bytes",\
	   "Time (s)",	"Time (us)");

  struct timespec time_s_h[ITER], time_e_h[ITER];
  struct timespec time_s_s[ITER], time_e_s[ITER];
  struct timespec time_s_m[ITER], time_e_m[ITER];
  
  for (ns = 0; ns < 1; ns++) {

    for (a=sizeof(parcel); a<=PHOTON_BUF_SIZE; a+=a) {
	  
			for (int ii = 0; ii < ITER; ii++) {
      
		    i = (int)a;
			
			  int* send_list;
			  int send_list_size;
			  parcel* pack = reinterpret_cast<parcel*> (send);
		  
		    if (rank != ns) {
					if (verbose) printf("------%d start receiving method in %d------\n", rank, ii);
		      transfer(rank, nproc, i, send, recv, lbuf, rbuf, lid, rid);
		    } 
		    else {
				  pack->source = ns;
					for (j=0; j<nproc; j++)
						pack->rank_list[j]=nproc-1-j; //make the list in reverse order
				  send_list = (int *)malloc(nproc * sizeof(int));
				  get_target(pack->source, rank, pack->rank_list, nproc, send_list, send_list_size);
		    }

		    // PUT
		    if (rank == ns) {
					//Hierarchical broadcast
					clock_gettime(CLOCK_MONOTONIC, &time_s_h[ii]);
					lbuf.addr = (uintptr_t)send;
					lbuf.size = i;
					lbuf.priv = (struct photon_buffer_priv_t){0,0};
					for (j=0; j<send_list_size; j++) {
						photon_put_with_completion(send_list[j], i, &lbuf, &rbuf[send_list[j]], lid, rid, 0);
						send_comp++;
						if (verbose) printf("%d send parcel to %d\n", rank, send_list[j]);
					}
					wait_local(send_comp);
					
					if (rank == ns) {
				  	clock_gettime(CLOCK_MONOTONIC, &time_e_h[ii]);
//						printf("%-7d", nproc);
//						printf("%-9u", ns);
//						printf("%-9s", "H");
//						printf("%-12u", i);
//				    double time_ns = (double)(((time_e.tv_sec - time_s.tv_sec) * 1e9) + (time_e.tv_nsec - time_s.tv_nsec));
//				    double time_ss = time_ns/1e9;
//				    double time_us = time_ns/1e3;
//						printf("%-12.2f", time_ss);
//						printf("%-14.2f\n", time_us);
//						fflush(stdout);
					}
				  if (rank == ns) {
				    send_done(nproc, rank);
				  }
					if (verbose) printf("------%d done with H method in %d------\n", rank, ii);
				}
				MPI_Barrier(MPI_COMM_WORLD);
				
		    if (rank != ns) {
					receive_once(rank, nproc, i, send, recv);
				}
				else {
					//Simple broadcast
				  if (rank == ns)
						clock_gettime(CLOCK_MONOTONIC, &time_s_s[ii]);
					lbuf.addr = (uintptr_t)send;
					lbuf.size = i;
					lbuf.priv = (struct photon_buffer_priv_t){0,0};
					for (j=0; j<nproc; j++) {
						if(j != rank) {
							photon_put_with_completion(j, i, &lbuf, &rbuf[j], lid, rid, 0);
							send_comp++;
							if (verbose) printf("%d send parcel to %d\n", rank, j);
						}
					}
					wait_local(send_comp);
				}
				MPI_Barrier(MPI_COMM_WORLD);	

				if (rank == ns) {
			  	clock_gettime(CLOCK_MONOTONIC, &time_e_s[ii]);
//						printf("%-7d", nproc);
//						printf("%-9u", ns);
//						printf("%-9s", "S");
//						printf("%-12u", i);
//				    double time_ns = (double)(((time_e.tv_sec - time_s.tv_sec) * 1e9) + (time_e.tv_nsec - time_s.tv_nsec));
//				    double time_ss = time_ns/1e9;
//				    double time_us = time_ns/1e3;
//						printf("%-12.2f", time_ss);
//						printf("%-14.2f\n", time_us);
//						fflush(stdout);
				}
				if (verbose) printf("------%d done with S method in %d------\n", rank, ii);
		    
				//MPI bcast
				MPI_Barrier(MPI_COMM_WORLD);	
		    if (rank == ns)
					clock_gettime(CLOCK_MONOTONIC, &time_s_m[ii]);
				MPI_Bcast(send, i, MPI_BYTE, ns, MPI_COMM_WORLD);
				if (verbose) printf("%d ran MPI\n", rank);
				MPI_Barrier(MPI_COMM_WORLD);
		    if (rank == ns)	{
				  clock_gettime(CLOCK_MONOTONIC, &time_e_m[ii]);
//					printf("%-7d", nproc);
//					printf("%-9u", ns);
//					printf("%-9s", "MPI");
//					printf("%-12u", i);
//			    double time_ns = (double)(((time_e.tv_sec - time_s.tv_sec) * 1e9) + (time_e.tv_nsec - time_s.tv_nsec));
//			    double time_ss = time_ns/1e9;
//			    double time_us = time_ns/1e3;
//					printf("%-12.2f", time_ss);
//					printf("%-14.2f\n", time_us);
//					fflush(stdout);
				}		
					if (verbose) printf("------%d done with MPI method in %d------\n", rank, ii);

//		    if (!a) a = 0.5;
		    
	  	}
		  
	    if (rank == ns) {
    		double time_ns_h(0.0);
    		double time_ns_s(0.0);
    		double time_ns_m(0.0);
				for(int iii = 0; iii < ITER; iii++){
					 time_ns_h = time_ns_h + (double)(((time_e_h[iii].tv_sec - time_s_h[iii].tv_sec) * 1e9) + (time_e_h[iii].tv_nsec - time_s_h[iii].tv_nsec));
					 time_ns_s = time_ns_s + (double)(((time_e_s[iii].tv_sec - time_s_s[iii].tv_sec) * 1e9) + (time_e_s[iii].tv_nsec - time_s_s[iii].tv_nsec));
					 time_ns_m = time_ns_m + (double)(((time_e_m[iii].tv_sec - time_s_m[iii].tv_sec) * 1e9) + (time_e_m[iii].tv_nsec - time_s_m[iii].tv_nsec));
				}
				time_ns_h = time_ns_h / ITER;
				time_ns_s = time_ns_s / ITER;
				time_ns_m = time_ns_m / ITER;
    
				printf("%-7d", nproc);
				printf("%-9u", ns);
				printf("%-9s", "H");
				printf("%-12u", i);
		    double time_ss = time_ns_h/1e9;
		    double time_us = time_ns_h/1e3;
				printf("%-12.2f", time_ss);
				printf("%-14.2f\n", time_us);
				
				printf("%-7d", nproc);
				printf("%-9u", ns);
				printf("%-9s", "S");
				printf("%-12u", i);
		    time_ss = time_ns_s/1e9;
		    time_us = time_ns_s/1e3;
				printf("%-12.2f", time_ss);
				printf("%-14.2f\n", time_us);
				
				printf("%-7d", nproc);
				printf("%-9u", ns);
				printf("%-9s", "MPI");
				printf("%-12u", i);
		    time_ss = time_ns_m/1e9;
		    time_us = time_ns_m/1e3;
				printf("%-12.2f", time_ss);
				printf("%-14.2f\n", time_us);
				
				fflush(stdout);
			}
    }
  }

  MPI_Barrier(MPI_COMM_WORLD);

 exit:
  photon_unregister_buffer(send, PHOTON_BUF_SIZE);
  free(send);
  //for (i=0; i<nproc; i++) {
  for (i=0; i<1; i++) {
    photon_unregister_buffer(recv[i], PHOTON_BUF_SIZE);
    free(recv[i]);
  }

  photon_finalize();
  MPI_Finalize();
  return 0;
}

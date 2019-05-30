/* 
 * 1D decomposition of the domain in the column dimension
 * such that each MPI task will get an equal number of rows.
 */

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef VIS
#include "vis_tmp.h"
#endif

#ifndef ROWS
#define ROWS		1000
#endif
#ifndef COLS
#define COLS		1000
#endif
#ifndef MAX_ITER
#define MAX_ITER	1000
#endif
#ifndef TOL
#define TOL		1e-2
#endif
#ifndef MAX_TEMP
#define MAX_TEMP	100.0
#endif
#ifndef CHKPNT_ITER
#define CHKPNT_ITER	100
#endif

float** allocate(int,int,float**);
void init_grid(int,int,float**,float);
void halo_update(int,int,float**);

int rank,numranks;

int main(int argc, char* argv[]){

    MPI_Init(&argc,&argv);
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD,&numranks);
    int last_rank = numranks-1;
    int t_rows=ROWS,t_cols=COLS;

    int max_iter = MAX_ITER,
        iter = 0,
        t_chkpoint=0;
    float max_temp = MAX_TEMP;
    int i,j;
    if (argc > 1){
        for (i=0; i < argc; i++){
            if (argv[i][0] == '-') {
                if (argv[i][1] == 'm'){
                    max_iter = atoi(argv[i+1]);
                }
                else if (argv[i][1] == 'r'){
                    t_rows = atoi(argv[i+1]);
                }
                else if (argv[i][1] == 'c'){
                    t_cols = atoi(argv[i+1]);
                }
                else if (argv[i][1] == 'h'){
                    max_temp = atof(argv[i+1]);
                }
            }
        }
    }

    int rows,
        cols = t_cols+2;
    int rem = t_rows % numranks;
    if ( rank == last_rank)
        rows = rem + ((t_rows - rem) / numranks) + 2; 
    else 
        rows = ((t_rows - rem) / numranks) + 2;

    float dT = max_temp,
          dT_local=max_temp;


    float**  T_old = allocate (rows,cols,T_old);
    float**  T_new = allocate (rows,cols,T_new);

    init_grid(rows,cols,T_old,max_temp);
    init_grid(rows,cols,T_new,max_temp);
#ifdef VIS
    if (rank == 0){
        create_raw_dir(numranks);
    }
#endif
    while ( dT > TOL && iter <= max_iter ){

#ifdef VIS
        if ( iter % CHKPNT_ITER == 0){
            dump_iter_bin(rank,numranks,rows,cols,T_new,iter);	
            t_chkpoint++;
        }
#endif
        halo_update(rows,cols,T_old);

        for (i=1 ; i < rows-1; i++){
            for(j=1; j < cols-1; j++){
                T_new[i][j] = 0.25 * (T_old[i-1][j] + T_old[i+1][j] + T_old[i][j-1] + T_old[i][j+1]); 
            }
        }

        dT = 0.0;
        dT_local=0.0;
        for (i=1 ; i < rows-1; i++){
            for(j=1; j < cols-1; j++){
                dT_local = fmaxf(fabsf(T_new[i][j] - T_old[i][j]),dT_local);
                T_old[i][j] = T_new[i][j];
            }
        }
        MPI_Allreduce(&dT_local, &dT , 1 , MPI_FLOAT, MPI_MAX, MPI_COMM_WORLD);

        iter++;
    }
    // Printing results
    if (rank == 0){
        if (iter-1 == max_iter)
            printf  ( "Reached maximum iterations %d. Error = %2.4f\n",iter-1,dT);
        else
            printf  ( "[JOBID: %s]: Converged in %d iterations with and error of %2.4f\n",getenv("SLURM_JOBID"),iter-1,dT);
    }

#ifdef VIS
    if (rank == 0){
        record_common_content(numranks,t_chkpoint,t_rows+2, cols);
    }
#endif
    MPI_Finalize();
    return 0;
}

float** allocate(int rows, int cols, float** T){
    int i=0,j=0;
    T = (float**) malloc(rows * sizeof(float*));
    if (T !=NULL) {
        T[0] = (float*) malloc(rows*cols*sizeof(float)); 
    }	
    if (T[0] != NULL){
        for (i =0 ; i< rows ; i++)
            T[i] = (*T + i*cols);
    }
    return T;
}

void init_grid(int rows, int cols, float** T, float max_temp){
    int i=0,j=0;
    int last_rank = numranks-1;

    for (i = 0; i < rows; i++){
        for (j = 0; j < cols; j++){
            T[i][j] = 0.0;
        }
    }
    // Set top and bottom boundary initial values
    if (rank == 0){
        for (j =0; j< cols; j++){
            T[0][j] = max_temp;
        }
    }
    else if (rank == last_rank){
        for (j =0; j< cols; j++){
            T[rows-1][j] = max_temp;
        }
    }
    // Set left and rigth boundary initial values
    for (i = 0; i < rows; i++){
        T[i][0] = max_temp;
        T[i][cols-1] = max_temp;
    }
}

void halo_update(int rows, int cols, float** T){

    int last_rank = numranks-1;
    MPI_Request req[4];
    MPI_Status status[4];

    if (rank != 0){
        MPI_Isend(&T[1][0],cols,MPI_FLOAT,rank-1, 100, MPI_COMM_WORLD,&req[0]);
        MPI_Irecv(&T[0][0],cols, MPI_FLOAT,rank-1, 100, MPI_COMM_WORLD,&req[1]);
    }
    else {
        MPI_Isend(&T[0][0],cols,MPI_FLOAT,MPI_PROC_NULL,100,MPI_COMM_WORLD,&req[0]);
        MPI_Irecv(&T[0][0],cols, MPI_FLOAT,MPI_PROC_NULL, 100, MPI_COMM_WORLD,&req[1]);
    }

    if (rank != last_rank){
        MPI_Isend(&T[rows-2][0],cols,MPI_FLOAT,rank+1,100,MPI_COMM_WORLD,&req[2]);
        MPI_Irecv(&T[rows-1][0],cols, MPI_FLOAT,rank+1,100, MPI_COMM_WORLD,&req[3]);
    }
    else{
        MPI_Isend(&T[rows-1][0],cols,MPI_FLOAT,MPI_PROC_NULL,100,MPI_COMM_WORLD,&req[2]);
        MPI_Irecv(&T[rows-1][0],cols, MPI_FLOAT,MPI_PROC_NULL,100, MPI_COMM_WORLD,&req[3]);
    }


    MPI_Waitall(4,req,status);

}

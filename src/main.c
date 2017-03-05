/*
 * INF560
 *
 * Image Filtering Project
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <gif_lib.h>
#include <mpi.h>
#include "filters.h"
#include <mpi.h>
#include <unistd.h>
#include "dispatch_util.h"

int main( int argc, char ** argv )
{
    /* I/O variables */
    char * input_filename ;
    char * output_filename ;
    // Loaded gif.
    animated_gif * image ;
    int numberOfImages;
    /* Variables for time measures */
    struct timeval t1, t2;
    double duration;
    /* General information for MPI */
    int rankInWorld, totalProcesses;
    /* Information for image Communicator - one communicator / images */
    MPI_Comm imageCommunicator;
    int color, rankInGroup, groupSize;
    /* Information for image processed by communicator */
    int width, height, size;
    pixel *picture;


    MPI_Init(&argc, &argv);
    if ( argc < 3 )
    {
        fprintf( stderr, "Usage: %s input.gif output.gif \n", argv[0] ) ;
        return 1 ;
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &rankInWorld);
    MPI_Comm_size(MPI_COMM_WORLD, &totalProcesses);
    if (rankInWorld == 0) {
      input_filename = argv[1] ;
      output_filename = argv[2] ;
      /* IMPORT Timer start */
      gettimeofday(&t1, NULL);
      /* Load file and store the pixels in array */
      image = load_pixels( input_filename ) ;
      if ( image == NULL ) { return 1 ; }

      /* IMPORT Timer stop */
      gettimeofday(&t2, NULL);

    duration = (t2.tv_sec -t1.tv_sec)+((t2.tv_usec-t1.tv_usec)/1e6);
     printf( "GIF loaded from file %s with %d image(s) in %lf s\n",
              input_filename, image->n_images, duration ) ;
      numberOfImages = image->n_images;
      width = image->width[0];
      height = image->height[0];
      /* IMPORT Timer start */
      gettimeofday(&t1, NULL);
    }

    //Broadcast number of images to everybody.
    MPI_Bcast(&numberOfImages, 1, MPI_INT, 0, MPI_COMM_WORLD);

    //fprintf(stderr, "Process %d knows that there are %d images. \n", rankInWorld, numberOfImages);
    if (numberOfImages > totalProcesses) {
       if (rankInWorld == 0) {
         fprintf(stderr, "Not enough processes, treating each image sequentially.\n");
         apply_gray_filter(image);
         apply_blur_filter(image, 5, 20);
         apply_sobel_filter(image);
         gettimeofday(&t2, NULL);
         duration = (t2.tv_sec -t1.tv_sec)+((t2.tv_usec-t1.tv_usec)/1e6);
         printf( "Filtering done in %lf s \n", duration) ;
         /* EXPORT Timer start */
         gettimeofday(&t1, NULL);
         /* Store file from array of pixels to GIF file */
         if ( !store_pixels( output_filename, image ) ) { return 1 ; }
         /* EXPORT Timer stop */
         gettimeofday(&t2, NULL);
         duration = (t2.tv_sec -t1.tv_sec)+((t2.tv_usec-t1.tv_usec)/1e6);
         printf( "Export done in %lf s in file %s\n", duration, output_filename ) ;
       }
      return 1;
    }
    //Create communicators
    int k = totalProcesses / numberOfImages;
    int r = totalProcesses - k * numberOfImages;
    if (rankInWorld <= (r - 1) * (k + 1) + k) {
      color = rankInWorld / (k + 1);
    } else {
      color = (rankInWorld - r) / k;
    }
    MPI_Comm_split(MPI_COMM_WORLD, color, rankInWorld, &imageCommunicator);
    MPI_Comm_rank(imageCommunicator, &rankInGroup);
    MPI_Comm_size(imageCommunicator, &groupSize);
    //fprintf(stderr, "Process  %d has been assigned to group %d with local rank %d. \n",rankInWorld, color, rankInGroup);

    //Send image to the root of each group.
    int c;
    if (rankInWorld == 0) {
      apply_gray_filter(image);
      apply_blur_filter(image, 5, 20);
      apply_sobel_filter(image);

      width = image->width[0];
      height = image->height[0];
      size = image->width[0] * image->height[0];
      picture = (pixel *)malloc(size * sizeof(pixel));
      copyImageIntoImage(image->p[0], picture, size);
      for (c = 1; c < r; c++) {
        sendImageToProcess(
          image->width[c],
          image->height[c],
          image->p[c],
          c * (k + 1));
      }
      for (c = r; c < numberOfImages; c++) {
        if (c == 0) continue;
        sendImageToProcess(
          image->width[c],
          image->height[c],
          image->p[c],
          c * k + r);
      }
    }

    //Receive image from root.
    if (rankInGroup == 0 && rankInWorld != 0) {
      receiveWidthAndHeightFromProcess(&width, &height, 0);
      size = width * height;
      picture = malloc(size * sizeof(pixel));
      receiveImageFromProcess(size, picture, 0);
    }
    // Processing each image

      //Send results back to root.
    if (rankInGroup == 0) {
      if (rankInWorld != 0) {
        sendImageToProcess(width, height, picture, 0);
        //sendGreyImageToProcessWithTagAndSize(picture, 0, rankInWorld, width * height);
      }
    }

    if (rankInWorld == 0) {
      // Get result back from other processes.
      copyImageIntoImage(picture, image->p[0], size);
      int c, widthRec, heightRec;
      for (c = 1; c < r; c++) {
        receiveWidthAndHeightFromProcess(&widthRec, &heightRec, c * (k + 1));
        receiveImageFromProcess(widthRec * heightRec, image->p[c], c * (k + 1));
      }
      for (c = r; c < numberOfImages; c++) {
        if (c == 0) continue;
        receiveWidthAndHeightFromProcess(&widthRec, &heightRec, c * k + r);
        receiveImageFromProcess(widthRec * heightRec, image->p[c], c * k + r);
      }

      //receiveGreyImageFromAllProcessesWithSize(image, r, k , numberOfImages);
      /* FILTERS Timer stops */
      gettimeofday(&t2, NULL);
      duration = (t2.tv_sec -t1.tv_sec)+((t2.tv_usec-t1.tv_usec)/1e6);
      printf( "Filtering done in %lf s \n", duration) ;
      /* EXPORT Timer start */
      gettimeofday(&t1, NULL);
      /* Store file from array of pixels to GIF file */
      if ( !store_pixels( output_filename, image ) ) { return 1 ; }
      /* EXPORT Timer stop */
      gettimeofday(&t2, NULL);
      duration = (t2.tv_sec -t1.tv_sec)+((t2.tv_usec-t1.tv_usec)/1e6);
      printf( "Export done in %lf s in file %s\n", duration, output_filename ) ;
    }

  //  MPI_Comm_free(&imageCommunicator);
    MPI_Finalize();
    return 0 ;
}

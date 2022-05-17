#include "transcoder.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        fprintf(stderr, "Example: ./qtranscoder /tmp/1.mp3 /tmp/test.mp4\n");
        exit(1);
    }

    new Transcoder(argv[1], argv[2]);

    return 0;
}

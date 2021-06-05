# constants 
root="/tmp/khliao"
image="disk_image"
image_size=64K
inodes=16

# Assumptions
# we assume the  script is ran in the working repository
# We assume that root is an existing empty directory

#prepare root and image
fusermount -u ${root}
truncate -s ${image_size} ${image}

# format and mount fs image
make

./mkfs.a1fs -f -i ${inodes} ${image}
./a1fs ${image} ${root}

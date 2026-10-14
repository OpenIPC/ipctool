#ifndef JFFS2_H
#define JFFS2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A JFFS2 image builder, for `ipctool upgrade` to carry a camera's settings
 * across a repartition. The settings live on a jffs2 overlay; when the layout
 * changes, the new rootfs_data starts somewhere else and the running kernel
 * has no partition there to mount, so the files are written as a ready-made
 * filesystem instead.
 *
 * What mkfs.jffs2 makes, minus everything optional: little-endian, nodes
 * uncompressed, data in page-sized pieces, a cleanmarker at the start of
 * every erase block, a padding node where the next one would cross into the
 * next block. No hard links (a second name is a second copy), and no
 * extended attributes: a tree with any is refused, since the one overlayfs
 * sets hides deleted files and dropping it brings them back.
 */

typedef struct jffs2_img jffs2_img_t;

/* An image of erase blocks of `erasesize` that may grow to `limit` bytes. */
jffs2_img_t *jffs2_new(size_t erasesize, size_t limit);
void jffs2_free(jffs2_img_t *img);

/* Every entry under the directory `path`, recursively, as the root of the
 * image. A directory named in `skip_contents` (relative to `path`, e.g.
 * "work") is created empty. False on a read error or when the image would
 * pass its limit; the reason is in jffs2_error(). */
bool jffs2_add_tree(jffs2_img_t *img, const char *path,
                    const char *skip_contents);

/* The image so far, padded to whole erase blocks. */
const uint8_t *jffs2_data(jffs2_img_t *img, size_t *len);
const char *jffs2_error(jffs2_img_t *img);
/* How many files, directories, links and device nodes it holds. */
int jffs2_entries(jffs2_img_t *img);

#endif /* JFFS2_H */

#ifndef VERSION_H
#define VERSION_H

extern const char *GIT_TAG;
extern const char *GIT_REV;
extern const char *GIT_BRANCH;

const char *get_git_version(void) { return GIT_TAG; }

const char *get_git_revision(void) { return GIT_REV; }

const char *get_git_branch(void) { return GIT_BRANCH; }

#endif /* VERSION_H */

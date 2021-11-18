#ifndef VERSION_H
#define VERSION_H

extern const char *GIT_TAG;
extern const char *GIT_REV;
extern const char *GIT_BRANCH;
extern const char *BUILDDATE;

static inline const char *get_git_version(void) { return GIT_TAG; }

static inline const char *get_git_revision(void) { return GIT_REV; }

static inline const char *get_git_branch(void) { return GIT_BRANCH; }

static inline const char *get_builddate(void) { return BUILDDATE; }

#endif /* VERSION_H */

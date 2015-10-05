#include "private/svn_diff_private.h"
  const svn_patch_t *patch;
  svn_boolean_t original_no_final_eol;
  svn_boolean_t modified_no_final_eol;
  const svn_patch_t *patch;
/* Common guts of svn_diff_hunk__create_adds_single_line() and
 * svn_diff_hunk__create_deletes_single_line().
 *
 * ADD is TRUE if adding and FALSE if deleting.
 */
static svn_error_t *
add_or_delete_single_line(svn_diff_hunk_t **hunk_out,
                          const char *line,
                          const svn_patch_t *patch,
                          svn_boolean_t add,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_diff_hunk_t *hunk = apr_palloc(result_pool, sizeof(*hunk));
  static const char *hunk_header[] = { "@@ -1 +0,0 @@\n", "@@ -0,0 +1 @@\n" };
  const apr_size_t header_len = strlen(hunk_header[add]);
  const apr_size_t len = strlen(line);
  const apr_size_t end = header_len + (1 + len); /* The +1 is for the \n. */
  svn_stringbuf_t *buf = svn_stringbuf_create_ensure(end + 1, scratch_pool);

  hunk->patch = patch;

  /* hunk->apr_file is created below. */

  hunk->diff_text_range.start = header_len;
  hunk->diff_text_range.current = header_len;

  if (add)
    {
      hunk->original_text_range.start = 0; /* There's no "original" text. */
      hunk->original_text_range.current = 0;
      hunk->original_text_range.end = 0;
      hunk->original_no_final_eol = FALSE;

      hunk->modified_text_range.start = header_len;
      hunk->modified_text_range.current = header_len;
      hunk->modified_text_range.end = end;
      hunk->modified_no_final_eol = TRUE;

      hunk->original_start = 0;
      hunk->original_length = 0;

      hunk->modified_start = 1;
      hunk->modified_length = 1;
    }
  else /* delete */
    {
      hunk->original_text_range.start = header_len;
      hunk->original_text_range.current = header_len;
      hunk->original_text_range.end = end;
      hunk->original_no_final_eol = TRUE;

      hunk->modified_text_range.start = 0; /* There's no "original" text. */
      hunk->modified_text_range.current = 0;
      hunk->modified_text_range.end = 0;
      hunk->modified_no_final_eol = FALSE;

      hunk->original_start = 1;
      hunk->original_length = 1;

      hunk->modified_start = 0;
      hunk->modified_length = 0; /* setting to '1' works too */
    }

  hunk->leading_context = 0;
  hunk->trailing_context = 0;

  /* Create APR_FILE and put just a hunk in it (without a diff header).
   * Save the offset of the last byte of the diff line. */
  svn_stringbuf_appendbytes(buf, hunk_header[add], header_len);
  svn_stringbuf_appendbyte(buf, add ? '+' : '-');
  svn_stringbuf_appendbytes(buf, line, len);
  svn_stringbuf_appendbyte(buf, '\n');
  svn_stringbuf_appendcstr(buf, "\\ No newline at end of hunk\n");

  hunk->diff_text_range.end = buf->len;

  SVN_ERR(svn_io_open_unique_file3(&hunk->apr_file, NULL /* filename */,
                                   NULL /* system tempdir */,
                                   svn_io_file_del_on_pool_cleanup,
                                   result_pool, scratch_pool));
  SVN_ERR(svn_io_file_write_full(hunk->apr_file,
                                 buf->data, buf->len,
                                 NULL, scratch_pool));
  /* No need to seek. */

  *hunk_out = hunk;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_hunk__create_adds_single_line(svn_diff_hunk_t **hunk_out,
                                       const char *line,
                                       const svn_patch_t *patch,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool)
{
  SVN_ERR(add_or_delete_single_line(hunk_out, line, patch, 
                                    (!patch->reverse),
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_hunk__create_deletes_single_line(svn_diff_hunk_t **hunk_out,
                                          const char *line,
                                          const svn_patch_t *patch,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool)
{
  SVN_ERR(add_or_delete_single_line(hunk_out, line, patch,
                                    patch->reverse,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

  const char *eol_p;
  apr_pool_t *last_pool;

  if (!eol)
    eol = &eol_p;
      *eol = NULL;

  /* It's not ITERPOOL because we use data allocated in LAST_POOL out
     of the loop. */
  last_pool = svn_pool_create(scratch_pool);
      svn_pool_clear(last_pool);

                                   last_pool, last_pool));
      SVN_ERR(svn_io_file_seek(file, APR_CUR, &range->current, last_pool));
      *eol = NULL;
  if (!filtered && *eof && !*eol && *str->data)
      /* Ok, we miss a final EOL in the patch file, but didn't see a
         no eol marker line.
         We should report that we had an EOL or the patch code will
         misbehave (and it knows nothing about no eol markers) */
      if (!no_final_eol && eol != &eol_p)
        {

      *eof = FALSE;
      /* Fall through to seek back to the right location */
  svn_pool_destroy(last_pool);
                                       hunk->patch->reverse
                                          ? hunk->modified_no_final_eol
                                          : hunk->original_no_final_eol,
                                       hunk->patch->reverse
                                          ? hunk->original_no_final_eol
                                          : hunk->modified_no_final_eol,
  const char *eol_p;

  if (!eol)
    eol = &eol_p;
      *eol = NULL;

  if (*eof && !*eol && *line->data)
    {
      /* Ok, we miss a final EOL in the patch file, but didn't see a
          no eol marker line.

          We should report that we had an EOL or the patch code will
          misbehave (and it knows nothing about no eol markers) */

      if (eol != &eol_p)
        {
          /* Lets pick the first eol we find in our patch file */
          apr_off_t start = 0;
          svn_stringbuf_t *str;

          SVN_ERR(svn_io_file_seek(hunk->apr_file, APR_SET, &start,
                                   scratch_pool));

          SVN_ERR(svn_io_file_readline(hunk->apr_file, &str, eol, NULL,
                                       APR_SIZE_MAX,
                                       scratch_pool, scratch_pool));

          /* Every patch file that has hunks has at least one EOL*/
          SVN_ERR_ASSERT(*eol != NULL);
        }

      *eof = FALSE;

      /* Fall through to seek back to the right location */
    }

  svn_boolean_t original_no_final_eol = FALSE;
  svn_boolean_t modified_no_final_eol = FALSE;
              /* Set for the type and context by using != the other type */
              if (last_line_type != modified_line)
                original_no_final_eol = TRUE;
              if (last_line_type != original_line)
                modified_no_final_eol = TRUE;
            continue; /* Proceed to the next line in the svn:mergeinfo hunk. */
          else
            {
              /* Perhaps we can also use original_lines/modified_lines here */

              in_hunk = FALSE; /* On to next property */
            }
      (*hunk)->original_no_final_eol = original_no_final_eol;
      (*hunk)->modified_no_final_eol = modified_no_final_eol;
   state_old_mode_seen,     /* old mode 100644 */
   state_git_mode_seen,     /* new mode 100644 */
/* Helper for git_old_mode() and git_new_mode().  Translate the git
 * file mode MODE_STR into a binary "executable?" and "symlink?" state. */
static svn_error_t *
parse_git_mode_bits(svn_tristate_t *executable_p,
                    svn_tristate_t *symlink_p,
                    const char *mode_str)
{
  apr_uint64_t mode;
  SVN_ERR(svn_cstring_strtoui64(&mode, mode_str,
                                0 /* min */,
                                0777777 /* max: six octal digits */,
                                010 /* radix (octal) */));

  /* Note: 0644 and 0755 are the only modes that can occur for plain files.
   * We deliberately choose to parse only those values: we are strict in what
   * we accept _and_ in what we produce.
   *
   * (Having said that, though, we could consider relaxing the parser to also
   * map
   *     (mode & 0111) == 0000 -> svn_tristate_false
   *     (mode & 0111) == 0111 -> svn_tristate_true
   *        [anything else]    -> svn_tristate_unknown
   * .)
   */

  switch (mode & 0777)
    {
      case 0644:
        *executable_p = svn_tristate_false;
        break;

      case 0755:
        *executable_p = svn_tristate_true;
        break;

      default:
        /* Ignore unknown values. */
        *executable_p = svn_tristate_unknown;
        break;
    }

  switch (mode & 0170000 /* S_IFMT */)
    {
      case 0120000: /* S_IFLNK */
        *symlink_p = svn_tristate_true;
        break;

      case 0100000: /* S_IFREG */
      case 0040000: /* S_IFDIR */
        *symlink_p = svn_tristate_false;
        break;

      default:
        /* Ignore unknown values.
           (Including those generated by Subversion <= 1.9) */
        *symlink_p = svn_tristate_unknown;
        break;
    }

  return SVN_NO_ERROR;
}

/* Parse the 'old mode ' line of a git extended unidiff. */
static svn_error_t *
git_old_mode(enum parse_state *new_state, char *line, svn_patch_t *patch,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(parse_git_mode_bits(&patch->old_executable_bit,
                              &patch->old_symlink_bit,
                              line + STRLEN_LITERAL("old mode ")));

#ifdef SVN_DEBUG
  /* If this assert trips, the "old mode" is neither ...644 nor ...755 . */
  SVN_ERR_ASSERT(patch->old_executable_bit != svn_tristate_unknown);
#endif

  *new_state = state_old_mode_seen;
  return SVN_NO_ERROR;
}

/* Parse the 'new mode ' line of a git extended unidiff. */
static svn_error_t *
git_new_mode(enum parse_state *new_state, char *line, svn_patch_t *patch,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(parse_git_mode_bits(&patch->new_executable_bit,
                              &patch->new_symlink_bit,
                              line + STRLEN_LITERAL("new mode ")));

#ifdef SVN_DEBUG
  /* If this assert trips, the "old mode" is neither ...644 nor ...755 . */
  SVN_ERR_ASSERT(patch->new_executable_bit != svn_tristate_unknown);
#endif

  /* Don't touch patch->operation. */

  *new_state = state_git_mode_seen;
  return SVN_NO_ERROR;
}

static svn_error_t *
git_index(enum parse_state *new_state, char *line, svn_patch_t *patch,
          apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  /* We either have something like "index 33e5b38..0000000" (which we just
     ignore as we are not interested in git specific shas) or something like
     "index 33e5b38..0000000 120000" which tells us the mode, that isn't
     changed by applying this patch.

     If the mode would have changed then we would see 'old mode' and 'new mode'
     lines.
  */
  line = strchr(line + STRLEN_LITERAL("index "), ' ');

  if (line && patch->new_executable_bit == svn_tristate_unknown
           && patch->new_symlink_bit == svn_tristate_unknown
           && patch->operation != svn_diff_op_added
           && patch->operation != svn_diff_op_deleted)
    {
      SVN_ERR(parse_git_mode_bits(&patch->new_executable_bit,
                                  &patch->new_symlink_bit,
                                  line + 1));

      /* There is no change.. so set the old values to the new values */
      patch->old_executable_bit = patch->new_executable_bit;
      patch->old_symlink_bit = patch->new_symlink_bit;
    }

  /* This function doesn't change the state! */
  /* *new_state = *new_state */
  return SVN_NO_ERROR;
}

  SVN_ERR(parse_git_mode_bits(&patch->new_executable_bit,
                              &patch->new_symlink_bit,
                              line + STRLEN_LITERAL("new file mode ")));

  SVN_ERR(parse_git_mode_bits(&patch->old_executable_bit,
                              &patch->old_symlink_bit,
                              line + STRLEN_LITERAL("deleted file mode ")));

  {"--- a/",            state_git_mode_seen,    git_minus},
  {"--- /dev/null",     state_git_mode_seen,    git_minus},
  {"old mode ",         state_git_diff_seen,    git_old_mode},
  {"new mode ",         state_old_mode_seen,    git_new_mode},

  {"rename from ",      state_git_mode_seen,    git_move_from},
  {"copy from ",        state_git_mode_seen,    git_copy_from},
  {"index ",            state_git_diff_seen,    git_index},
  {"index ",            state_git_tree_seen,    git_index},
  {"index ",            state_git_mode_seen,    git_index},

  {"GIT binary patch",  state_git_mode_seen,    binary_patch_start},
  patch->old_executable_bit = svn_tristate_unknown;
  patch->new_executable_bit = svn_tristate_unknown;
  patch->old_symlink_bit = svn_tristate_unknown;
  patch->new_symlink_bit = svn_tristate_unknown;
      else if ((state == state_git_tree_seen || state == state_git_mode_seen)
               && line_after_tree_header_read
               && !valid_header_line)
          /* We have a valid diff header for a patch with only tree changes.
           * Rewind to the start of the line just read, so subsequent calls
           * to this function don't end up skipping the line -- it may
           * contain a patch. */
          SVN_ERR(svn_io_file_seek(patch_file->apr_file, APR_SET, &last_line,
                                   scratch_pool));
          break;
      else if (state == state_git_tree_seen
               || state == state_git_mode_seen)
               && state != state_git_diff_seen)
      svn_tristate_t ts_tmp;


      ts_tmp = patch->old_executable_bit;
      patch->old_executable_bit = patch->new_executable_bit;
      patch->new_executable_bit = ts_tmp;

      ts_tmp = patch->old_symlink_bit;
      patch->old_symlink_bit = patch->new_symlink_bit;
      patch->new_symlink_bit = ts_tmp;
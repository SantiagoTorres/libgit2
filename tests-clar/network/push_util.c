
#include "clar_libgit2.h"
#include "buffer.h"
#include "vector.h"
#include "push_util.h"

const git_oid OID_ZERO = {{ 0 }};

void updated_tip_free(updated_tip *t)
{
	git__free(t->name);
	git__free(t->old_oid);
	git__free(t->new_oid);
	git__free(t);
}

void record_callbacks_data_clear(record_callbacks_data *data)
{
	size_t i;
	updated_tip *tip;

	git_vector_foreach(&data->updated_tips, i, tip)
		updated_tip_free(tip);

	git_vector_free(&data->updated_tips);
}

int record_update_tips_cb(const char *refname, const git_oid *a, const git_oid *b, void *data)
{
	updated_tip *t;
	record_callbacks_data *record_data = (record_callbacks_data *)data;

	cl_assert(t = git__malloc(sizeof(*t)));

	cl_assert(t->name = git__strdup(refname));
	cl_assert(t->old_oid = git__malloc(sizeof(*t->old_oid)));
	git_oid_cpy(t->old_oid, a);

	cl_assert(t->new_oid = git__malloc(sizeof(*t->new_oid)));
	git_oid_cpy(t->new_oid, b);

	git_vector_insert(&record_data->updated_tips, t);

	return 0;
}

int delete_ref_cb(git_remote_head *head, void *payload)
{
	git_vector *delete_specs = (git_vector *)payload;
	git_buf del_spec = GIT_BUF_INIT;

	/* Ignore malformed ref names (which also saves us from tag^{} */
	if (!git_reference_is_valid_name(head->name))
		return 0;

	/* Create a refspec that deletes a branch in the remote */
	cl_git_pass(git_buf_putc(&del_spec, ':'));
	cl_git_pass(git_buf_puts(&del_spec, head->name));

	cl_git_pass(git_vector_insert(delete_specs, git_buf_detach(&del_spec)));

	return 0;
}

int record_ref_cb(git_remote_head *head, void *payload)
{
	git_vector *refs = (git_vector *) payload;
	return git_vector_insert(refs, head);
}

void verify_remote_refs(git_vector *actual_refs, const expected_ref expected_refs[], size_t expected_refs_len)
{
	/* If there are no refs on the remote, git returns capabilities in a dummy
	 * ref pkt-line.
	 */
	static const expected_ref no_refs_result[] = { { "capabilities^{}", &OID_ZERO } };

	size_t i;

	git_buf msg = GIT_BUF_INIT;
	git_remote_head *actual;
	char *oid_str;

	if (expected_refs_len == 0) {
		/* Special case when no refs exist on remote. */
		expected_refs = no_refs_result;
		expected_refs_len = ARRAY_SIZE(no_refs_result);
	}

	if (expected_refs_len != actual_refs->length)
		goto failed;

	for(i = 0; i < expected_refs_len; i++) {
		git_remote_head *curr_actual = git_vector_get(actual_refs, i);

		if (
			strcmp(expected_refs[i].name, curr_actual->name)
			|| git_oid_cmp(expected_refs[i].oid, &curr_actual->oid)
		)
			goto failed;
	}

	return;

failed:
	git_buf_puts(&msg, "Expected and actual refs differ:\nEXPECTED:\n");

	for(i = 0; i < expected_refs_len; i++) {
		cl_assert(oid_str = git_oid_allocfmt(expected_refs[i].oid));
		cl_git_pass(git_buf_printf(&msg, "%s = %s\n", expected_refs[i].name, oid_str));
		git__free(oid_str);
	}

	git_buf_puts(&msg, "\nACTUAL:\n");
	git_vector_foreach(actual_refs, i, actual) {
		cl_assert(oid_str = git_oid_allocfmt(&actual->oid));
		cl_git_pass(git_buf_printf(&msg, "%s = %s\n", actual->name, oid_str));
		git__free(oid_str);
	}

	cl_fail(git_buf_cstr(&msg));

	git_buf_free(&msg);
}
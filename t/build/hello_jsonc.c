#include <json.h>

int main (int argc, char **argv)
{
	json_object *o = json_object_new_object ();
	if (!o)
		return 1;
	json_object_put (o);

	return 0;
}

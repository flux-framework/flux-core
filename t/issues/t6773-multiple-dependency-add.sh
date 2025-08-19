#!/bin/sh
#
#  Jobs with dependencies are mishandled during instance restart because
#  jobtap callbacks are made multiple times per job


cat <<EOF >t6773.py
import flux
import flux.job

handle = flux.Flux()
print("stopping queue")
handle.rpc(
    "job-manager.queue-start",
    {"start": False, "all": True, "nocheckpoint": False},
    nodeid=0,
).get()

print("submitting sleep job")
jobspec = flux.job.JobspecV1.from_command(["/bin/true"])
jobid = flux.job.submit(handle, jobspec)
scheme = "afterany"
print("submitting chain of 9 dependent jobs")
for i in range(9):
    dependency = [{"scheme": f"{scheme}", "value": f"{jobid}"}]
    jobspec.setattr("attributes.system.dependencies", dependency)
    jobspec.setattr("attributes.system.job.name", f"{scheme}-{i}")
    jobid = flux.job.submit(handle, jobspec)

EOF

# Run restartable instance and submit 10 dependent jobs:
rm -rf statedir.t6773
mkdir -p statedir.t6773

echo "Running restartable instance"
flux start -Sstatedir=statedir.t6773 flux python t6773.py

# Create script for restarted instance that:
# - starts the queue
# - walks all jobs ensuring they ran and had only one dependency-add and
#   one flux-restart event each.
cat <<-'EOF' >t6773.sh
#!/bin/bash
RC=0
flux queue start
flux watch --all || RC=1
let count=0
for id in $(flux jobs -ano {id}); do
    flux job status -vvv $id || RC=1
    test $(flux job eventlog $id | grep -c flux-restart) -le 1 || RC=1
    test $(flux job eventlog $id | grep -c dependency-add) -le 1 || RC=1
    let count++
done
echo processed $count jobs
exit $RC
EOF
chmod +x t6773.sh

flux start -Sstatedir=statedir.t6773 ./t6773.sh


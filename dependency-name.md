# Dependency Name

This is a branch of flux-core that provides an ability to specify a dependency name (the actual name, not the job id) to be given
to a second job, with the goal of the second job waiting for the first to finish (afterok, meaning only on success) before starting.
Here are some brief instructions for how to get this working locally in a development environment.

## Usage

We are going to make a frobnicator plugin. I'm still not sure what "frobnicator" means, so let's reference some of our previous wisdom:

> Frobnicator "frobs jobspec at job submission time"

And

> frob jobspec knobs to swab JSON blobs stopping user sobs at job submission time. 

But we cannot be certain if the frobnicator is truly enough to stop the sobs! I digress. We are going to write a plugin of this type because it will mutate the jobspec before letting it pass through. 

### 1. Update broker.toml

This means that the first thing we need to do is register the plugin in our [broker.toml](broker.toml). That means a section (in toml) that looks like this:

```toml
[ingest.frobnicator]
plugins = [ "defaults", "constraints", "dependency" ]
```

The first two are done by default, and our custom plugin is called "dependency," which means the file "dependency.py" is added to `src/bindings/python/flux/job/frobnicator/plugins`.

Note that the broker.toml also assumes having:

- resource `R`
- a Curve Certificate
- the hostname of your host or development container (change this manually)

To generate the first two:

```bash
flux R encode --local > /tmp/R
flux keygen /tmp/curve.cert
```

### 2. Start the broker

We can run `flux start` and provide our custom broker config to start the flux broker.

```bash
flux start -o --config=./broker.toml 
```
That should run without an error. Then try listing plugins.

```bash
flux job-frobnicator --list-plugins
```
```console
Available plugins:
constraints           Apply constraints to incoming jobspec based on broker config.
defaults              Apply defaults to incoming jobspec based on broker config.
dependency            Translate dependency.name into a job id for dependency.afterok
```

### 3. Submit Jobs

We next are going to submit jobs, with one that depends on the first. Here is our first job:

```bash
flux submit --job-name sleepy sleep 10
```

And immediately after, run:

```bash
flux run --setattr=dependency.name=sleepy hostname
```
```console
epy hostname
flux-job: ƒ8CAShwy9 resolving dependencies                              00:00:08
```

Another way to see this is the same, but with submit, and then `flux jobs -a`

```bash
flux submit --job-name sleepy sleep 10
flux submit --setattr=dependency.name=sleepy hostname
flux jobs -a
```
```console
$ flux jobs -a
       JOBID USER     NAME       ST NTASKS NNODES     TIME INFO
   ƒ8cveUCsy vscode   hostname    D      1      -        - depends:after-success=16821021310976
   ƒ8crs43Aw vscode   sleepy      R      1      1   0.237s 3013fe118c9d
```

You'll see that it is waiting, above.
Note that for this plugin design:

1. The first matching name is chosen.
2. If the dependency name isn't defined, no change in behavior
3. If a name is defined and not found, you get an error (see below)

```bash
$ flux submit --setattr=dependency.name=doesnotexist hostname
[Errno 1] Job with name doesnotexist is not known
```

And that's it! This was pretty easy / fun to write. Thanks to Tom for the hackathon today and everyone that attended and dealt with my dumb questions and having to look at my face. 🤪️

### x. Development

To work on this, I figured out how the dependency (as it actually works) would be added to a jobspec with a dry run:

```bash
flux submit --dry-run --dependency=after:"$(flux job id $(flux job last))" hostname | jq
```

That showed me it was under `jobspec.attributes['system']` and an attribute like `dependency.name=value` would get transformed into `{"dependency": {"name": "value"}}`

Likely for experiments I'll do a custom view build with flux that has this module.
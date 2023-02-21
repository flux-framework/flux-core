#!/usr/bin/env python3
# Directives embedded in python docstring
def main():
    """
    flux: -N4
    flux: --queue=batch
    flux: --job-name="my python job"

    # Set some arbitrary user data:
    flux: --setattr=user.data='''
    flux: x, y, z
    flux: a, b, c
    flux: '''
    """
    run()


if __name__ == "__main__":
    main()

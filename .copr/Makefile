# COPR "make srpm" build method: COPR runs `make -f .copr/Makefile srpm outdir=...`
# in a checkout of the repository. We pack the checkout itself, so a build
# works for any commit without publishing a release tarball first.

spec := packaging/alveus.spec
version := $(shell sed -n 's/^Version:[[:space:]]*//p' $(spec))
top := $(shell git rev-parse --show-toplevel)

srpm:
	dnf -y install git rpm-build
	git config --global --add safe.directory $(top)
	mkdir -p $(outdir)
	cd $(top) && git archive --format=tar.gz --prefix=alveus-plasma-$(version)/ \
		-o /tmp/alveus-plasma-$(version).tar.gz HEAD
	rpmbuild -bs $(top)/$(spec) \
		--define "_sourcedir /tmp" \
		--define "_srcrpmdir $(outdir)"

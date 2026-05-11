SPHINXBUILD = sphinx-build
SOURCEDIR   = source
BUILDDIR    = _build

.PHONY: help html clean

help:
	@$(SPHINXBUILD) -M help $(SOURCEDIR) $(BUILDDIR)

html:
	@$(SPHINXBUILD) -M html $(SOURCEDIR) $(BUILDDIR)
	@echo "Build finished. Open $(BUILDDIR)/html/index.html"

clean:
	rm -rf $(BUILDDIR)

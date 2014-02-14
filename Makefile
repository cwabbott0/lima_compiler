.PHONY: all standalone lib clean src/glsl

all: standalone lib

src/glsl:
	$(MAKE) all -C src/glsl

standalone: src/glsl
	$(MAKE) standalone -C src/lima

lib: src/glsl
	$(MAKE) lib -C src/lima

clean:
	$(MAKE) clean -C src/glsl
	$(MAKE) clean -C src/lima

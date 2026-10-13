import { forwardRef, useEffect, useImperativeHandle, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { RoundedBoxGeometry } from 'three/examples/jsm/geometries/RoundedBoxGeometry.js';
import { applyMove, AXIS_NORMALS, GEOMETRY, inLayer, trackStickers, type Move } from '../lib/cube/cube';
import { UNKNOWN_HEX } from '../lib/cube/colors';

export interface Cube3DHandle {
  /** Animates the moves in order; resolves when finished (or stopped). */
  play: (moves: Move[], opts?: { msPerTurn?: number; onMove?: (index: number) => void }) => Promise<void>;
  /** Finishes the current animation instantly and drops queued moves. */
  stop: () => void;
  /** Shows a state without animation. */
  setState: (facelets: string, highlight?: number[]) => void;
}

interface Props {
  facelets: string;
  colors: Record<string, string>; // facelet letter -> hex
  highlight?: number[];
  interactive?: boolean;
  autoRotate?: boolean;
  className?: string;
  label?: string;
}

const ease = (t: number) => (t < 0.5 ? 4 * t * t * t : 1 - (-2 * t + 2) ** 3 / 2);

export const Cube3D = forwardRef<Cube3DHandle, Props>(function Cube3D(
  { facelets, colors, highlight = [], interactive = true, autoRotate = false, className, label },
  ref,
) {
  const mountRef = useRef<HTMLDivElement>(null);
  const engine = useRef<CubeEngine | null>(null);

  useEffect(() => {
    const el = mountRef.current;
    if (!el) return;
    const e = new CubeEngine(el, interactive);
    engine.current = e;
    return () => {
      e.dispose();
      engine.current = null;
    };
  }, [interactive]);

  // Idle updates from props (ignored while an animation is running).
  useEffect(() => {
    engine.current?.setColors(colors);
    engine.current?.setStateIfIdle(facelets, highlight);
  }, [facelets, colors, highlight]);

  useEffect(() => {
    engine.current?.setAutoRotate(autoRotate);
  }, [autoRotate]);

  useImperativeHandle(ref, () => ({
    play: (moves, opts) => engine.current?.play(moves, opts) ?? Promise.resolve(),
    stop: () => engine.current?.stop(),
    setState: (f, h = []) => engine.current?.setState(f, h),
  }), []);

  return <div ref={mountRef} className={className} role="img" aria-label={label ?? 'Rubik’s cube'} />;
});

// Imperative three.js scene, kept outside React's render cycle.
class CubeEngine {
  private renderer: THREE.WebGLRenderer;
  private scene = new THREE.Scene();
  private camera = new THREE.PerspectiveCamera(32, 1, 0.1, 100);
  private root = new THREE.Group();
  private cubies: { group: THREE.Group; home: THREE.Vector3 }[] = [];
  private stickers: THREE.Mesh<THREE.ShapeGeometry, THREE.MeshStandardMaterial>[] = [];
  private controls: OrbitControls | null = null;
  private resize: ResizeObserver;
  private state = '';
  private highlight: number[] = [];
  private colors: Record<string, string> = {};
  private animating = false;
  private cancel = false;
  private autoRotate = false;
  private aimed = false;
  followFocus = true;
  private frame = 0;
  private disposed = false;
  private disposables: { dispose: () => void }[] = [];

  constructor(private el: HTMLElement, interactive: boolean) {
    this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    el.appendChild(this.renderer.domElement);

    this.camera.position.set(4.7, 3.9, 6.3);
    this.camera.lookAt(0, 0, 0);
    this.scene.add(new THREE.AmbientLight(0xffffff, 1.6));
    const key = new THREE.DirectionalLight(0xffffff, 1.4);
    key.position.set(4, 8, 6);
    this.scene.add(key);
    const fill = new THREE.DirectionalLight(0xffffff, 0.5);
    fill.position.set(-6, -2, -4);
    this.scene.add(fill);
    this.scene.add(this.root);
    this.buildCube();

    if (interactive) {
      this.controls = new OrbitControls(this.camera, this.renderer.domElement);
      this.controls.enablePan = false;
      this.controls.enableZoom = false;
      this.controls.rotateSpeed = 0.6;
      this.controls.addEventListener('change', () => this.render());
    }
    this.resize = new ResizeObserver(() => this.onResize());
    this.resize.observe(el);
    this.onResize();
  }

  private buildCube() {
    const body = new RoundedBoxGeometry(0.96, 0.96, 0.96, 3, 0.09);
    const bodyMat = new THREE.MeshStandardMaterial({ color: 0x17191d, roughness: 0.55 });
    const shape = roundedSquare(0.8, 0.13);
    const stickerGeo = new THREE.ShapeGeometry(shape, 6);
    this.disposables.push(body, bodyMat, stickerGeo);
    const groups = new Map<string, THREE.Group>();
    for (let x = -1; x <= 1; x++) {
      for (let y = -1; y <= 1; y++) {
        for (let z = -1; z <= 1; z++) {
          if (x === 0 && y === 0 && z === 0) continue;
          const g = new THREE.Group();
          g.position.set(x, y, z);
          g.add(new THREE.Mesh(body, bodyMat));
          this.root.add(g);
          groups.set(`${x},${y},${z}`, g);
          this.cubies.push({ group: g, home: new THREE.Vector3(x, y, z) });
        }
      }
    }
    const zAxis = new THREE.Vector3(0, 0, 1);
    GEOMETRY.forEach((geo) => {
      const mat = new THREE.MeshStandardMaterial({ color: UNKNOWN_HEX, roughness: 0.35 });
      this.disposables.push(mat);
      const m = new THREE.Mesh(stickerGeo, mat);
      const n = new THREE.Vector3(...geo.normal);
      m.position.copy(n.clone().multiplyScalar(0.487));
      m.quaternion.setFromUnitVectors(zAxis, n);
      groups.get(geo.pos.join(','))!.add(m);
      this.stickers.push(m);
    });
  }

  private onResize() {
    const w = this.el.clientWidth || 1;
    const h = this.el.clientHeight || 1;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    // Keep the whole cube in view on narrow screens.
    this.camera.fov = w / h < 0.9 ? 40 : 32;
    this.camera.updateProjectionMatrix();
    this.render();
  }

  setColors(colors: Record<string, string>) {
    this.colors = colors;
    this.paint();
  }

  setState(facelets: string, highlight: number[]) {
    const changedFocus = highlight.join() !== this.highlight.join() || !this.aimed;
    this.state = facelets;
    this.highlight = highlight;
    this.paint();
    if (changedFocus && this.followFocus) {
      this.aimed = true;
      this.aimCamera();
    }
  }

  setStateIfIdle(facelets: string, highlight: number[]) {
    if (!this.animating) this.setState(facelets, highlight);
  }

  setAutoRotate(on: boolean) {
    this.autoRotate = on;
    if (on) this.loop();
  }

  private paint() {
    const spotlight = new Set(this.highlight);
    const dim = spotlight.size > 0;
    const grey = new THREE.Color(0x7a7e85);
    this.stickers.forEach((m, i) => {
      const hex = this.colors[this.state[i]] ?? UNKNOWN_HEX;
      const c = new THREE.Color(hex);
      const focused = dim && spotlight.has(i);
      if (dim && !focused) c.lerp(grey, 0.42);
      m.material.color.copy(c);
      // The focused piece glows softly in its own colour.
      m.material.emissive.copy(focused ? new THREE.Color(hex).multiplyScalar(0.28) : new THREE.Color(0x000000));
    });
    this.render();
  }

  // Swing the camera towards the piece in focus (e.g. under the cube for a
  // bottom-layer edge) while keeping the front and top recognisable.
  private aimCamera() {
    const base = new THREE.Vector3(4.7, 3.9, 6.3);
    const dist = base.length();
    const centroid = new THREE.Vector3();
    this.highlight.forEach((i) => {
      const g = GEOMETRY[i];
      if (g) centroid.add(new THREE.Vector3(...g.pos)).add(new THREE.Vector3(...g.normal).multiplyScalar(0.5));
    });
    let dir = base.clone().normalize();
    // Only move when most of the focused stickers face away from the default
    // view (bottom, left or back), so ordinary steps keep a steady camera.
    const hidden = this.highlight.filter((i) => {
      const n = GEOMETRY[i]?.normal;
      return n && n[0] * dir.x + n[1] * dir.y + n[2] * dir.z <= 0;
    }).length;
    if (hidden * 2 > this.highlight.length && centroid.lengthSq() > 0.01) {
      dir = dir.add(centroid.normalize().multiplyScalar(1.1)).normalize();
    }
    const target = dir.multiplyScalar(dist);
    if (target.distanceTo(this.camera.position) < 0.05) return;
    const from = this.camera.position.clone();
    const start = performance.now();
    const step = (now: number) => {
      if (this.disposed) return;
      const t = Math.min(1, (now - start) / 700);
      // Move along the sphere, not through the cube.
      this.camera.position.copy(from).lerp(target, ease(t)).setLength(dist);
      this.camera.lookAt(0, 0, 0);
      this.controls?.update();
      this.render();
      if (t < 1) requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
  }

  stop() {
    this.cancel = true;
  }

  async play(moves: Move[], opts: { msPerTurn?: number; onMove?: (index: number) => void } = {}) {
    if (this.animating) this.stop();
    while (this.animating) await new Promise((r) => setTimeout(r, 10));
    this.cancel = false;
    this.animating = true;
    try {
      for (let i = 0; i < moves.length; i++) {
        if (this.cancel || this.disposed) break;
        opts.onMove?.(i);
        await this.animateOne(moves[i], opts.msPerTurn ?? 320);
      }
    } finally {
      this.animating = false;
    }
  }

  private animateOne(move: Move, msPerTurn: number): Promise<void> {
    return new Promise((resolve) => {
      const axis = new THREE.Vector3(...AXIS_NORMALS[move.axis]);
      const angle = move.turns === 2 ? -Math.PI : move.turns === 1 ? -Math.PI / 2 : Math.PI / 2;
      const duration = move.turns === 2 ? msPerTurn * 1.4 : msPerTurn;
      const pivot = new THREE.Group();
      this.root.add(pivot);
      const layer = this.cubies.filter((c) => inLayer(move.axis, [c.home.x, c.home.y, c.home.z]));
      layer.forEach((c) => pivot.attach(c.group));
      const start = performance.now();

      const finish = () => {
        layer.forEach((c) => {
          this.root.attach(c.group);
          c.group.position.copy(c.home);
          c.group.quaternion.identity();
        });
        this.root.remove(pivot);
        // The rotation ends where the permuted colours put each sticker, so
        // resetting the cubies and repainting shows the same picture.
        this.state = applyMove(this.state, move);
        this.highlight = trackStickers(this.highlight, move);
        this.paint();
        resolve();
      };

      const step = (now: number) => {
        if (this.disposed) return resolve();
        const t = this.cancel ? 1 : Math.min(1, (now - start) / duration);
        pivot.quaternion.setFromAxisAngle(axis, angle * ease(t));
        this.render();
        if (t < 1) requestAnimationFrame(step);
        else finish();
      };
      requestAnimationFrame(step);
    });
  }

  private loop = () => {
    if (!this.autoRotate || this.disposed) return;
    this.root.rotation.y += 0.0025;
    this.render();
    this.frame = requestAnimationFrame(this.loop);
  };

  private render() {
    if (!this.disposed) this.renderer.render(this.scene, this.camera);
  }

  dispose() {
    this.disposed = true;
    cancelAnimationFrame(this.frame);
    this.resize.disconnect();
    this.controls?.dispose();
    this.disposables.forEach((d) => d.dispose());
    this.renderer.dispose();
    this.renderer.domElement.remove();
  }
}

function roundedSquare(size: number, r: number): THREE.Shape {
  const s = size / 2;
  const shape = new THREE.Shape();
  shape.moveTo(-s + r, -s);
  shape.lineTo(s - r, -s);
  shape.quadraticCurveTo(s, -s, s, -s + r);
  shape.lineTo(s, s - r);
  shape.quadraticCurveTo(s, s, s - r, s);
  shape.lineTo(-s + r, s);
  shape.quadraticCurveTo(-s, s, -s, s - r);
  shape.lineTo(-s, -s + r);
  shape.quadraticCurveTo(-s, -s, -s + r, -s);
  return shape;
}

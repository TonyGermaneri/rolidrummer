export interface Versioned<T> { value: T; revision: number }
export interface WriteResult<T> { ok: boolean; field: Versioned<T> }

/** A view writes against what it displayed, never a revision it merely polled.
 * Remote changes wait until a gesture finishes. A rejected transaction replaces
 * the local draft; closing a view never writes a stale snapshot. */
export class SharedView<T> {
  private displayed = 0
  private latest?: Versioned<T>
  private editing = false
  private pending?: T
  private saving?: Promise<void>
  private disposed = false

  constructor(
    private write: (value: T, revision: number) => Promise<WriteResult<T>>,
    private apply: (value: T) => void,
  ) {}

  receive(field?: Versioned<T>) {
    if (!field || this.disposed) return
    if (!this.latest || field.revision >= this.latest.revision) this.latest = field
    this.adopt()
  }

  begin() { if (!this.disposed) this.editing = true }

  cancel() { this.editing = false; this.adopt() }

  finish(value: T): Promise<void> {
    if (this.disposed || !this.editing) return this.saving ?? Promise.resolve()
    this.editing = false
    this.pending = JSON.parse(JSON.stringify(value))
    if (!this.saving) {
      this.saving = this.flush().finally(() => {
        this.saving = undefined
        this.adopt()
      })
    }
    return this.saving
  }

  private async flush() {
    while (this.pending !== undefined && !this.disposed) {
      const value = this.pending
      this.pending = undefined
      const result = await this.write(value, this.displayed)
      if (this.disposed) return
      if (!this.latest || result.field.revision >= this.latest.revision) this.latest = result.field
      if (!result.ok) {
        this.pending = undefined
        this.editing = false
        // Force application even if the conflicting state was already polled.
        this.displayed = -1
        return
      }
      this.displayed = result.field.revision
    }
  }

  private adopt() {
    if (this.editing || this.saving || this.disposed || !this.latest) return
    if (this.latest.revision > this.displayed) {
      this.displayed = this.latest.revision
      this.apply(JSON.parse(JSON.stringify(this.latest.value)))
    }
  }

  dispose() { this.disposed = true; this.pending = undefined }
}

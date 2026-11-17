import * as React from 'react'
import { cva, type VariantProps } from 'class-variance-authority'
import { cn } from '@/lib/utils'

const alertVariants = cva(
  'relative w-full rounded-md border px-3 py-2 text-sm [&>svg]:absolute [&>svg]:left-3 [&>svg]:top-2.5 [&>svg]:size-4 [&>svg~*]:pl-6',
  {
    variants: {
      variant: {
        default: 'bg-card text-card-foreground border-border',
        info: 'border-primary/40 bg-primary/10 text-foreground [&>svg]:text-primary',
        warning: 'border-warning/50 bg-warning/10 text-foreground [&>svg]:text-warning',
        destructive: 'border-destructive/50 bg-destructive/10 text-foreground [&>svg]:text-destructive',
      },
    },
    defaultVariants: { variant: 'default' },
  },
)

const Alert = React.forwardRef<
  HTMLDivElement,
  React.HTMLAttributes<HTMLDivElement> & VariantProps<typeof alertVariants>
>(({ className, variant, ...props }, ref) => (
  <div ref={ref} role="alert" className={cn(alertVariants({ variant }), className)} {...props} />
))
Alert.displayName = 'Alert'

const AlertTitle = React.forwardRef<HTMLParagraphElement, React.HTMLAttributes<HTMLHeadingElement>>(
  ({ className, ...props }, ref) => (
    <h5 ref={ref} className={cn('mb-0.5 font-medium leading-none tracking-tight', className)} {...props} />
  ),
)
AlertTitle.displayName = 'AlertTitle'

const AlertDescription = React.forwardRef<HTMLParagraphElement, React.HTMLAttributes<HTMLParagraphElement>>(
  ({ className, ...props }, ref) => (
    <div ref={ref} className={cn('text-sm [&_p]:leading-relaxed', className)} {...props} />
  ),
)
AlertDescription.displayName = 'AlertDescription'

export { Alert, AlertTitle, AlertDescription }

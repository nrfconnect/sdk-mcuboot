def IMAGE_TAG = "ncs-toolchain:1.07"
def REPO_CI_TOOLS = "https://github.com/zephyrproject-rtos/ci-tools.git"

pipeline {
  agent {
    docker {
      image "$IMAGE_TAG"
      label "docker && ncs"
    }
  }
  options {
    // Checkout the repository to this folder instead of root
    checkoutToSubdirectory('mcuboot')
  }

  environment {
      // This token is used to by check_compliance to comment on PRs and use checks
      GH_TOKEN = credentials('nordicbuilder-compliance-token')
      GH_USERNAME = "NordicBuilder"
      COMPLIANCE_SCRIPT_PATH = "../ci-tools/scripts/check_compliance.py"
      COMPLIANCE_ARGS = "-r NordicPlayground/fw-nrfconnect-mcuboot"
      COMPLIANCE_REPORT_ARGS = "-p $CHANGE_ID -S $GIT_COMMIT -g"
  }

  stages {
    stage('Checkout repositories') {
      steps {
        dir("ci-tools") {
          git branch: "master", url: "$REPO_CI_TOOLS"
        }
      }
    }

    stage('Run compliance check') {
      steps {
        dir('mcuboot') {
          script {
            // If we're a pull request, compare the target branch against the current HEAD (the PR)
            if (env.CHANGE_TARGET) {
              COMMIT_RANGE = "origin/${env.CHANGE_TARGET}..HEAD"
              COMPLIANCE_ARGS = "$COMPLIANCE_ARGS $COMPLIANCE_REPORT_ARGS"
            }
            // If not a PR, it's a non-PR-branch or master build. Compare against the origin.
            else {
              COMMIT_RANGE = "origin/${env.BRANCH_NAME}..HEAD"
            }
            // Run the compliance check
            try {
              sh "$COMPLIANCE_SCRIPT_PATH $COMPLIANCE_ARGS --commits $COMMIT_RANGE"
            }
            finally {
              junit 'compliance.xml'
              archiveArtifacts artifacts: 'compliance.xml'
            }
          }
        }
      }
    }
  }

  post {
    always {
      // Clean up the working space at the end (including tracked files)
      cleanWs()
    }
    failure {
      script{
        if (env.BRANCH_NAME == 'master' || env.BRANCH_NAME.startsWith("PR"))
        {
            emailext(to: 'anpu',
                body: "${currentBuild.currentResult}\nJob ${env.JOB_NAME}\t\t build ${env.BUILD_NUMBER}\r\nLink: ${env.BUILD_URL}",
                subject: "[Jenkins][Build ${currentBuild.currentResult}: ${env.JOB_NAME}]",
                mimeType: 'text/html',)
        }
        else
        {
            echo "Branch ${env.BRANCH_NAME} is not master nor PR. Sending failure email skipped."
        }
      }
    }
  }
}
